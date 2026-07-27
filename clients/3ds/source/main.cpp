// finlink for Nintendo 3DS: by default, top screen shows the GBA stream,
// bottom screen shows Menu/Settings before connecting and a status +
// "Trennen" button while playing -- the 3DS's dual screens map onto
// Menu/Settings/Player far more naturally than the single-screen-at-a-time
// approach the other clients use. See clients/3ds/README.md.
//
// Which physical screen actually gets the video (vs. the status/"Trennen"
// UI) can flip once connected: either via Prefs::bottomScreenVideo (a
// user setting, single-screen stream types only), or unconditionally for a
// stream_type that's itself a dual-screen source's own secondary screen
// (docs/protocol.md "Stream-Typen") -- see useBottomForVideo below.
//
// No on-screen touch overlay for GBA input here (unlike Android/Switch):
// the 3DS's physical buttons already match the GBA's layout, and the
// bottom screen is needed for Menu/Settings/status instead.
//
// No borealis/Compose-equivalent UI framework here either: widgets are
// hand-rolled citro2d rects/text with manual touch hit-testing (ui.hpp).

#include <array>
#include <atomic>
#include <cstdlib>
#include <malloc.h>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <3ds.h>
#include <citro2d.h>

#include "finlink/handshake.h"

#include "audio.hpp"
#include "discovery.hpp"
#include "gba_buttons.hpp"
#include "prefs.hpp"
#include "session.hpp"
#include "ui.hpp"
#include "video_tex.hpp"

namespace {

constexpr int kPlayerBasePort = 6801;
constexpr int kPlayerSlotCount = 4;
constexpr u32 kSocBufferSize = 0x100000;
// ~0.6s at 60Hz, same value/reasoning as clients/nds's EXIT_HOLD_TICKS_REQUIRED
// and clients/switch's kExitHoldSeconds -- held rather than a single tap to
// avoid an accidental mid-game disconnect. X/Y have no GBA equivalent (see
// gba_buttons.hpp) so this doesn't take anything away from the game; unlike
// the "Trennen" touch button, it also still works once useBottomForVideo
// (see main()) has moved that button's screen under the video where the
// touchscreen itself no longer reaches it -- the 3DS's touch panel is fixed
// to the physical bottom screen regardless of which C3D_RenderTarget the UI
// is actually drawn to.
constexpr int kExitHoldTicksRequired = 36;

enum class BottomScreenState { MENU, SETTINGS };

// Everything the background search/discovery threads write, read by the
// main loop each frame. One coarse mutex: updates are infrequent (once
// per search/probe), so there's no reason to split it up further.
struct MenuState {
    std::mutex mutex;

    std::string hostText;
    bool searching = false;
    std::string statusText = "Nicht verbunden.";
    std::array<std::optional<bool>, kPlayerSlotCount> slotOccupied {};
    bool pickerVisible = false;
    std::string lastSearchedHost;

    bool discovering = false;
    std::string discoveryStatusText;
    std::vector<std::string> discoveredHosts;
};

void runSearch(MenuState *menu, std::string host) {
    {
        std::lock_guard<std::mutex> lock(menu->mutex);
        if (menu->searching) {
            return;
        }
        menu->searching = true;
        menu->pickerVisible = false;
        menu->statusText = "Suche...";
    }

    std::thread([menu, host]() {
        std::array<std::optional<bool>, kPlayerSlotCount> occupied;
        for (int slot = 0; slot < kPlayerSlotCount; slot++) {
            occupied[slot] = discovery::fetchOccupied(host, kPlayerBasePort + slot);
        }

        std::lock_guard<std::mutex> lock(menu->mutex);
        menu->lastSearchedHost = host;
        menu->searching = false;
        menu->slotOccupied = occupied;
        menu->pickerVisible = true;

        bool anyFree = false;
        bool anyReachable = false;
        for (const auto &o : occupied) {
            if (o.has_value()) {
                anyReachable = true;
                if (!*o) {
                    anyFree = true;
                }
            }
        }
        // Distinguishes "connected fine, every slot is just taken" from
        // "couldn't reach a single one of the four ports" -- both used to
        // show the same "kein freier Slot" message, which made a dead
        // connection look identical to a genuinely full host.
        if (anyFree) {
            menu->statusText = "Freien Slot waehlen.";
        } else if (anyReachable) {
            menu->statusText = "Kein freier Slot auf diesem Host.";
        } else {
            menu->statusText = "Host nicht erreichbar (Port 6801-6804).";
        }
    }).detach();
}

void startDiscovery(MenuState *menu) {
    {
        std::lock_guard<std::mutex> lock(menu->mutex);
        if (menu->discovering) {
            return;
        }
        menu->discovering = true;
        menu->discoveredHosts.clear();
        menu->discoveryStatusText = "Suche laeuft...";
    }

    std::thread([menu]() {
        auto hosts = discovery::localSubnetHosts();
        if (hosts.empty()) {
            std::lock_guard<std::mutex> lock(menu->mutex);
            menu->discovering = false;
            menu->discoveryStatusText = "Kein lokales Netzwerk gefunden.";
            return;
        }

        // Probing all 254 hosts one at a time (up to 400ms each) would
        // take up to ~100 seconds -- split the range across a handful of
        // worker threads instead. 8 rather than Android's 32: the 3DS's
        // CPU/RAM are much more limited.
        constexpr int kWorkers = 8;
        std::atomic<size_t> nextIndex { 0 };
        std::vector<std::thread> workers;
        for (int w = 0; w < kWorkers; w++) {
            workers.emplace_back([&hosts, &nextIndex, menu]() {
                for (;;) {
                    size_t i = nextIndex.fetch_add(1);
                    if (i >= hosts.size()) {
                        break;
                    }
                    if (discovery::probeLobby(hosts[i])) {
                        std::lock_guard<std::mutex> lock(menu->mutex);
                        menu->discoveredHosts.push_back(hosts[i]);
                    }
                }
            });
        }
        for (auto &t : workers) {
            t.join();
        }

        std::lock_guard<std::mutex> lock(menu->mutex);
        menu->discovering = false;
        menu->discoveryStatusText =
            menu->discoveredHosts.empty() ? "Nichts gefunden." : "Suche abgeschlossen.";
    }).detach();
}

// Blocking software-keyboard prompt for the host IP -- must be called
// outside C3D_FrameBegin/End, the applet draws its own frames while up.
std::string promptForHost(const std::string &initial) {
    SwkbdState swkbd;
    char buf[64];
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, sizeof(buf) - 1);
    swkbdSetHintText(&swkbd, "IP-Adresse, z.B. 192.168.1.5");
    if (!initial.empty()) {
        swkbdSetInitialText(&swkbd, initial.c_str());
    }
    SwkbdButton button = swkbdInputText(&swkbd, buf, sizeof(buf));
    if (button == SWKBD_BUTTON_CONFIRM) {
        return std::string(buf);
    }
    return initial;
}

void drawMenuScreen(C2D_TextBuf textBuf, const ui::Touch &touch, MenuState *menu, BottomScreenState *screenState,
                     GbaSession *session, VideoTex *videoTex, AudioPlayer *audio, std::atomic<bool> *connected,
                     std::string *connectedHost, std::string *connectedStreamType) {
    // Snapshot under a short lock, then draw/hit-test from local copies --
    // promptForHost() below blocks for as long as the user is typing, and
    // runSearch()/startDiscovery() spawn threads that take menu->mutex
    // themselves, so the lock can't be held across any of those calls.
    std::string hostText, statusText, discoveryStatusText, lastSearchedHost;
    bool searching, pickerVisible, discovering;
    std::array<std::optional<bool>, kPlayerSlotCount> slotOccupied;
    std::vector<std::string> discoveredHosts;
    {
        std::lock_guard<std::mutex> lock(menu->mutex);
        hostText = menu->hostText;
        searching = menu->searching;
        statusText = menu->statusText;
        slotOccupied = menu->slotOccupied;
        pickerVisible = menu->pickerVisible;
        lastSearchedHost = menu->lastSearchedHost;
        discovering = menu->discovering;
        discoveryStatusText = menu->discoveryStatusText;
        discoveredHosts = menu->discoveredHosts;
    }

    ui::Rect hostRect { 8, 8, 240, 28 };
    ui::drawRect(hostRect, ui::kColorButtonDisabled);
    ui::drawText(textBuf, hostText.empty() ? "Host eingeben..." : hostText.c_str(), hostRect.x + 8, hostRect.y + 6,
                 0.5f, hostText.empty() ? ui::kColorTextDim : ui::kColorText);
    if (touch.tappedIn(hostRect)) {
        std::string newHost = promptForHost(hostText);
        std::lock_guard<std::mutex> lock(menu->mutex);
        menu->hostText = newHost;
    }

    ui::Rect connectRect { 252, 8, 60, 28 };
    if (ui::button(textBuf, touch, connectRect, "Los", !searching && !hostText.empty())) {
        runSearch(menu, hostText);
    }

    if (pickerVisible) {
        for (int slot = 0; slot < kPlayerSlotCount; slot++) {
            ui::Rect r { 8.0f + slot * 76.0f, 44, 70, 26 };
            bool free = slotOccupied[slot].has_value() && !*slotOccupied[slot];
            char label[4];
            snprintf(label, sizeof(label), "P%d", slot + 1);
            if (ui::button(textBuf, touch, r, label, free)) {
                int port = kPlayerBasePort + slot;
                *connectedHost = lastSearchedHost;
                // videoTex is one long-lived object shared across every
                // connection this app makes (unlike Android/Switch, which
                // get a fresh one each time) -- without resetting it here,
                // reconnecting would keep showing the previous stream's
                // last frame until the new one's first (always full) frame
                // arrives.
                videoTex->reset();
                session->connect(lastSearchedHost, port,
                    GbaSession::Listener {
                        // Written from the session's background thread, in
                        // this order (streamType before the connected flag)
                        // so the main thread never observes connected=true
                        // with a stale/empty streamType -- same
                        // flag-guarded-publish pattern connected itself
                        // already relies on.
                        .onConnected =
                            [connected, connectedStreamType](std::string streamType) {
                                *connectedStreamType = std::move(streamType);
                                *connected = true;
                            },
                        .onVideoFrame =
                            [videoTex](uint32_t w, uint32_t h, std::vector<uint8_t> rgb) {
                                videoTex->setFrame(w, h, rgb);
                            },
                        .onAudioFrame =
                            [audio](uint32_t rate, uint8_t ch, std::vector<int16_t> pcm) {
                                audio->play(rate, ch, std::move(pcm));
                            },
                        .onDisconnected =
                            [connected, menu](std::string reason) {
                                *connected = false;
                                std::lock_guard<std::mutex> l(menu->mutex);
                                menu->statusText = "Fehler: " + reason;
                            },
                    });
            }
        }
    }

    ui::drawText(textBuf, statusText.c_str(), 8, 74, 0.42f, ui::kColorTextDim);

    // Diagnostic: if this console has no real LAN IP, no connect attempt
    // below is ever going to succeed, and that looks identical from the UI
    // (both "kein freier Slot" and an endless discovery scan) to a
    // perfectly healthy network where nothing just happens to answer --
    // this line is the difference.
    std::string myIp = discovery::localIpString();
    std::string netLine = myIp.empty() ? "Kein Netzwerk" : ("IP: " + myIp);
    ui::drawText(textBuf, netLine.c_str(), 190, 74, 0.38f, myIp.empty() ? ui::kColorButtonHeld : ui::kColorTextDim);

    ui::Rect discRect { 8, 96, 304, 24 };
    if (ui::button(textBuf, touch, discRect, "Netzwerk durchsuchen", !discovering)) {
        startDiscovery(menu);
    }
    ui::drawText(textBuf, discoveryStatusText.c_str(), 8, 124, 0.42f, ui::kColorTextDim);

    int shown = 0;
    for (const auto &ip : discoveredHosts) {
        if (shown >= 3) {
            break;
        }
        ui::Rect r { 8, 140.0f + shown * 20.0f, 304, 18 };
        if (ui::button(textBuf, touch, r, ip.c_str())) {
            {
                std::lock_guard<std::mutex> lock(menu->mutex);
                menu->hostText = ip;
            }
            runSearch(menu, ip);
        }
        shown++;
    }
    if (static_cast<int>(discoveredHosts.size()) > shown) {
        char more[32];
        snprintf(more, sizeof(more), "+%d weitere", static_cast<int>(discoveredHosts.size()) - shown);
        ui::drawText(textBuf, more, 8, 200, 0.4f, ui::kColorTextDim);
    }

    ui::Rect settingsRect { 8, 210, 304, 24 };
    if (ui::button(textBuf, touch, settingsRect, "Einstellungen")) {
        *screenState = BottomScreenState::SETTINGS;
    }
}

void drawSettingsScreen(C2D_TextBuf textBuf, const ui::Touch &touch, Prefs *prefs, VideoTex *videoTex,
                         BottomScreenState *screenState) {
    ui::drawText(textBuf, "Einstellungen", 8, 8, 0.55f, ui::kColorText);

    ui::drawText(textBuf, "Bilineare Filterung", 8, 44, 0.45f, ui::kColorText);
    ui::Rect t2 { 250, 40, 60, 28 };
    if (ui::toggle(touch, t2, prefs->bilinearVideoFilter)) {
        prefs->bilinearVideoFilter = !prefs->bilinearVideoFilter;
        prefs->save();
        videoTex->setBilinearFilter(prefs->bilinearVideoFilter);
    }

    ui::drawText(textBuf, "Bild auf unterem Bildschirm", 8, 84, 0.45f, ui::kColorText);
    ui::Rect t3 { 250, 80, 60, 28 };
    if (ui::toggle(touch, t3, prefs->bottomScreenVideo)) {
        prefs->bottomScreenVideo = !prefs->bottomScreenVideo;
        prefs->save();
    }
    // Only affects single-screen stream types (GC_GBA_LINK today) -- see
    // this file's own top comment and useBottomForVideo in main().
    ui::drawText(textBuf, "(wirkungslos bei Streams, die selbst schon",
                 8, 112, 0.36f, ui::kColorTextDim);
    ui::drawText(textBuf, "ein Zweitbildschirm-Inhalt sind)", 8, 128, 0.36f, ui::kColorTextDim);

    ui::Rect backRect { 8, 210, 304, 24 };
    if (ui::button(textBuf, touch, backRect, "Zurueck")) {
        *screenState = BottomScreenState::MENU;
    }
}

} // namespace

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    srand(static_cast<unsigned>(svcGetSystemTick()));

    gfxInitDefault();
    gfxSet3D(false);

    // If this fails, every network call below silently does nothing --
    // there's no separate error path for that here, but it shows up
    // directly as discovery::localIpString() reporting no IP on the Menu
    // screen (gethostid() needs a working soc service too).
    u32 *socBuf = static_cast<u32 *>(memalign(0x1000, kSocBufferSize));
    if (socBuf) {
        socInit(socBuf, kSocBufferSize);
    }

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    C3D_RenderTarget *topTarget = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bottomTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    C2D_TextBuf textBuf = C2D_TextBufNew(4096);

    Prefs prefs;
    MenuState menu;
    GbaSession session;
    VideoTex videoTex;
    AudioPlayer audio;
    videoTex.setBilinearFilter(prefs.bilinearVideoFilter);

    BottomScreenState screenState = BottomScreenState::MENU;
    // Written from the session's background thread (onConnected/
    // onDisconnected callbacks) and read every frame on the main thread.
    std::atomic<bool> connected { false };
    std::string connectedHost;
    std::string connectedStreamType;
    uint16_t physicalMask = 0;
    ui::Touch touch;
    int exitHoldTicks = 0;

    while (aptMainLoop()) {
        hidScanInput();
        touchPosition rawTouch;
        hidTouchRead(&rawTouch);
        touch.wasDown = touch.down;
        touch.down = (hidKeysHeld() & KEY_TOUCH) != 0;
        touch.x = rawTouch.px;
        touch.y = rawTouch.py;

        if (connected) {
            u32 held = hidKeysHeld();
            uint16_t newMask = 0;
            for (const auto &b : GBA_BUTTONS) {
                if (held & b.key) {
                    newMask |= b.bit;
                }
            }
            physicalMask = newMask;

            // X+Y held together disconnects -- see kExitHoldTicksRequired.
            if ((held & (KEY_X | KEY_Y)) == (KEY_X | KEY_Y)) {
                if (++exitHoldTicks >= kExitHoldTicksRequired) {
                    session.disconnect();
                    connected = false;
                    exitHoldTicks = 0;
                }
            } else {
                exitHoldTicks = 0;
            }
        } else {
            exitHoldTicks = 0;
        }

        videoTex.upload();
        C2D_TextBufClear(textBuf);

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        // Which physical screen gets the video vs. the status/menu/settings
        // UI -- see this file's own top comment. Pre-connect, video (or the
        // "finlink" placeholder) always stays on top and the UI on bottom,
        // same as always: connectedStreamType is only known once connected,
        // and there's nothing stream-type-specific to show before then
        // anyway.
        const bool useBottomForVideo =
            connected && (prefs.bottomScreenVideo ||
                          finlink_stream_type_prefers_secondary_screen(connectedStreamType.c_str()));
        C3D_RenderTarget *videoTarget = useBottomForVideo ? bottomTarget : topTarget;
        C3D_RenderTarget *uiTarget = useBottomForVideo ? topTarget : bottomTarget;
        const float videoTargetWidth = useBottomForVideo ? 320.0f : 400.0f;

        C2D_TargetClear(videoTarget, ui::kColorBg);
        C2D_SceneBegin(videoTarget);
        if (connected && videoTex.hasFrame()) {
            videoTex.drawFitted(0, 0, videoTargetWidth, 240);
        } else {
            ui::drawText(textBuf, "finlink", 150, 100, 0.9f, ui::kColorText);
            if (connected) {
                ui::drawText(textBuf, "Warte auf Bild...", 130, 140, 0.5f, ui::kColorTextDim);
            }
        }

        C2D_TargetClear(uiTarget, ui::kColorBg);
        C2D_SceneBegin(uiTarget);

        if (connected) {
            ui::drawText(textBuf, "Verbunden", 8, 90, 0.55f, ui::kColorText);
            ui::drawText(textBuf, "Physische Tasten sind aktiv.", 8, 120, 0.42f, ui::kColorTextDim);
            ui::drawText(textBuf, "X+Y halten zum Trennen.", 8, 140, 0.42f, ui::kColorTextDim);
            ui::Rect disconnectRect { 8, 206, 304, 26 };
            if (ui::button(textBuf, touch, disconnectRect, "Trennen")) {
                session.disconnect();
                connected = false;
            }
            session.sendInput(physicalMask);
        } else if (screenState == BottomScreenState::MENU) {
            drawMenuScreen(textBuf, touch, &menu, &screenState, &session, &videoTex, &audio, &connected,
                           &connectedHost, &connectedStreamType);
        } else {
            drawSettingsScreen(textBuf, touch, &prefs, &videoTex, &screenState);
        }

        C3D_FrameEnd(0);
    }

    session.disconnect();
    C2D_TextBufDelete(textBuf);
    C2D_Fini();
    C3D_Fini();
    if (socBuf) {
        socExit();
        free(socBuf);
    }
    gfxExit();
    return 0;
}
