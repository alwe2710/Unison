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

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
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
#include "strings_generated.hpp"
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

// Every stream_type docs/protocol.md currently defines ("Stream-Typen"),
// for the per-console antialiasing list in drawSettingsScreen() --
// Prefs::bilinearFor()/setBilinearFor() key off the same raw strings, this
// is just the fixed set + a human-readable label for each. streamType-only
// (not the label itself) so this can stay constexpr: the strings::kConsoleXxx
// globals it used to embed directly are runtime-mutable now (repointed by
// strings::setLanguage()), so the label is resolved fresh via
// labelForStreamType() below instead of being baked in once at compile time.
struct StreamTypeEntry {
    const char *streamType;
};
constexpr StreamTypeEntry kKnownStreamTypes[] = {
    { "GC_GBA_LINK" },
    { "WIIU_GAMEPAD" },
    { "N3DS_BOTTOM_SCREEN" },
    { "NDS_BOTTOM_SCREEN" },
};

const char *labelForStreamType(const char *streamType) {
    if (strcmp(streamType, "GC_GBA_LINK") == 0) {
        return strings::kConsoleGcGbaLink;
    }
    if (strcmp(streamType, "WIIU_GAMEPAD") == 0) {
        return strings::kConsoleWiiuGamepad;
    }
    if (strcmp(streamType, "N3DS_BOTTOM_SCREEN") == 0) {
        return strings::kConsoleN3dsBottomScreen;
    }
    return strings::kConsoleNdsBottomScreen; // NDS_BOTTOM_SCREEN
}

enum class BottomScreenState { MENU, SETTINGS, LANGUAGE, VIDEO_MODE };

// strings::kStatusError ("Fehler: %s") applied -- small helper since this
// is needed at both onDisconnected call sites below.
std::string formatError(const std::string &reason) {
    char buf[192];
    snprintf(buf, sizeof(buf), strings::kStatusError, reason.c_str());
    return buf;
}

// Everything the background search/discovery threads write, read by the
// main loop each frame. One coarse mutex: updates are infrequent (once
// per search/probe), so there's no reason to split it up further.
struct MenuState {
    std::mutex mutex;

    std::string hostText;
    bool searching = false;
    std::string statusText = strings::kStatusDisconnected;
    std::array<std::optional<bool>, kPlayerSlotCount> slotOccupied {};
    bool pickerVisible = false;
    std::string lastSearchedHost;
    // Set by drawMenuScreen() on a host-field tap, consumed by main()'s loop
    // right after C3D_FrameEnd() -- promptForHost() below is a blocking
    // swkbd applet call that draws its own frames, which conflicts with an
    // already-open C3D frame if called from inside drawMenuScreen() itself
    // (main() calls it between C3D_FrameBegin/C3D_FrameEnd).
    bool hostPromptRequested = false;
};

void runSearch(MenuState *menu, std::string host) {
    {
        std::lock_guard<std::mutex> lock(menu->mutex);
        if (menu->searching) {
            return;
        }
        menu->searching = true;
        menu->pickerVisible = false;
        menu->statusText = strings::kDiscoveryScanning;
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
            menu->statusText = strings::kLobbyPick;
        } else if (anyReachable) {
            menu->statusText = strings::kLobbyNoneConfigured;
        } else {
            menu->statusText = strings::kLobbyHostUnreachable;
        }
    }).detach();
}


// Blocking software-keyboard prompt for the host IP -- must be called
// outside C3D_FrameBegin/End, the applet draws its own frames while up.
std::string promptForHost(const std::string &initial) {
    SwkbdState swkbd;
    char buf[64];
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, sizeof(buf) - 1);
    swkbdSetHintText(&swkbd, strings::kHostHintExample);
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
                     std::string *connectedHost, std::string *connectedStreamType, Prefs *prefs,
                     std::string *connectedGrantedVideoMode, discovery::BeaconListener *beaconListener) {
    // Snapshot under a short lock, then draw/hit-test from local copies --
    // promptForHost() below blocks for as long as the user is typing, and
    // runSearch() spawns a thread that takes menu->mutex itself, so the
    // lock can't be held across either.
    std::string hostText, statusText, lastSearchedHost;
    bool searching, pickerVisible;
    std::array<std::optional<bool>, kPlayerSlotCount> slotOccupied;
    {
        std::lock_guard<std::mutex> lock(menu->mutex);
        hostText = menu->hostText;
        searching = menu->searching;
        statusText = menu->statusText;
        slotOccupied = menu->slotOccupied;
        pickerVisible = menu->pickerVisible;
        lastSearchedHost = menu->lastSearchedHost;
    }
    // BeaconListener has its own internal locking (see discovery.hpp) --
    // no need to route this through menu->mutex too.
    std::vector<discovery::DiscoveredServer> discoveredServers = beaconListener->snapshot();

    ui::Rect hostRect { 8, 8, 240, 28 };
    ui::drawRect(hostRect, ui::kColorButtonDisabled);
    ui::drawText(textBuf, hostText.empty() ? strings::kHostHint : hostText.c_str(), hostRect.x + 8, hostRect.y + 6,
                 0.5f, hostText.empty() ? ui::kColorTextDim : ui::kColorText);
    if (touch.tappedIn(hostRect)) {
        // Deferred to main()'s loop, right after C3D_FrameEnd() -- see
        // MenuState::hostPromptRequested.
        std::lock_guard<std::mutex> lock(menu->mutex);
        menu->hostPromptRequested = true;
    }

    ui::Rect connectRect { 252, 8, 60, 28 };
    if (ui::button(textBuf, touch, connectRect, strings::kMenuConnect, !searching && !hostText.empty())) {
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
                session->connect(lastSearchedHost, port, prefs->videoMode,
                    GbaSession::Listener {
                        // Written from the session's background thread, in
                        // this order (streamType/grantedVideoMode before
                        // the connected flag) so the main thread never
                        // observes connected=true with stale/empty values
                        // -- same flag-guarded-publish pattern connected
                        // itself already relies on.
                        .onConnected =
                            [connected, connectedStreamType, connectedGrantedVideoMode](
                                std::string streamType, std::string grantedVideoMode) {
                                *connectedStreamType = std::move(streamType);
                                *connectedGrantedVideoMode = std::move(grantedVideoMode);
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
                                menu->statusText = formatError(reason);
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
    std::string netLine;
    if (myIp.empty()) {
        netLine = strings::kDiscoveryNoNetwork;
    } else {
        char buf[80];
        snprintf(buf, sizeof(buf), strings::kDiscoveryOwnIp, myIp.c_str());
        netLine = buf;
    }
    ui::drawText(textBuf, netLine.c_str(), 190, 74, 0.38f, myIp.empty() ? ui::kColorButtonHeld : ui::kColorTextDim);

    // No manual "search" trigger -- a server announces itself via UDP
    // beacon roughly every 2s on its own (docs/protocol.md), so
    // BeaconListener (running continuously in the background, started/
    // stopped alongside socInit()/socExit() in main()) always has whatever
    // it's heard lately.
    ui::drawText(textBuf, discoveredServers.empty() ? strings::kDiscoverySearchingPlaceholder : strings::kDiscoveryFoundHeader,
                 8, 100, 0.42f, ui::kColorTextDim);

    int shown = 0;
    for (const auto &srv : discoveredServers) {
        if (shown >= 3) {
            break;
        }
        ui::Rect r { 8, 118.0f + shown * 22.0f, 304, 20 };
        std::string label = srv.gameTitle.empty() ? srv.host : srv.gameTitle;
        if (!srv.compatible) {
            label += strings::kDiscoveryIncompatibleSuffix;
        }
        if (ui::button(textBuf, touch, r, label.c_str(), srv.compatible)) {
            if (srv.streamType == "GC_GBA_LINK") {
                // Only GC_GBA_LINK has more than one slot (docs/protocol.md)
                // -- runSearch() probes PLAYER_BASE_PORT+0..3 for occupancy
                // and shows the P1-P4 picker below, exactly like tapping
                // "Los" after typing this same host manually would.
                {
                    std::lock_guard<std::mutex> lock(menu->mutex);
                    menu->hostText = srv.host;
                }
                runSearch(menu, srv.host);
            } else {
                // Every other stream type is single-client and connects
                // straight to the beacon's own handshake_port instead --
                // probing PLAYER_BASE_PORT+0..3 against a server that was
                // never Dolphin doesn't find "free"/"occupied" slots, just
                // four unreachable ports (matches the Android/Switch
                // clients' own MenuActivity/discovery dispatch).
                {
                    std::lock_guard<std::mutex> lock(menu->mutex);
                    menu->hostText = srv.host;
                }
                *connectedHost = srv.host;
                // videoTex is one long-lived object shared across every
                // connection this app makes -- without resetting it here,
                // reconnecting would keep showing the previous stream's
                // last frame until the new one's first (always full) frame
                // arrives (same reasoning as the P1-P4 picker's own connect
                // call below).
                videoTex->reset();
                session->connect(srv.host, srv.handshakePort, prefs->videoMode,
                    GbaSession::Listener {
                        .onConnected =
                            [connected, connectedStreamType, connectedGrantedVideoMode](
                                std::string streamType, std::string grantedVideoMode) {
                                *connectedStreamType = std::move(streamType);
                                *connectedGrantedVideoMode = std::move(grantedVideoMode);
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
                                menu->statusText = formatError(reason);
                            },
                    });
            }
        }
        shown++;
    }
    if (static_cast<int>(discoveredServers.size()) > shown) {
        char more[32];
        snprintf(more, sizeof(more), strings::kDiscoveryMoreServers, static_cast<int>(discoveredServers.size()) - shown);
        ui::drawText(textBuf, more, 8, 118.0f + shown * 22.0f, 0.4f, ui::kColorTextDim);
    }

    ui::Rect settingsRect { 8, 210, 304, 24 };
    if (ui::button(textBuf, touch, settingsRect, strings::kSettings)) {
        *screenState = BottomScreenState::SETTINGS;
    }
}

// Prefs::LanguagePref::SYSTEM resolves to whichever of DE/FR/IT/ES the
// console's own system language (CFGU_GetSystemLanguage) matches --
// anything else (including the call failing, or a system language this
// app has no translation for, e.g. Japanese) falls back to English, same
// policy as every other client. An explicit DE/EN/FR/IT/ES is an override
// from drawSettingsScreen()'s language button below.
strings::Lang resolveLanguage(const Prefs &prefs) {
    switch (prefs.language) {
    case Prefs::LanguagePref::DE: return strings::Lang::DE;
    case Prefs::LanguagePref::EN: return strings::Lang::EN;
    case Prefs::LanguagePref::FR: return strings::Lang::FR;
    case Prefs::LanguagePref::IT: return strings::Lang::IT;
    case Prefs::LanguagePref::ES: return strings::Lang::ES;
    default: break;
    }
    u8 sysLanguage = 0;
    if (R_SUCCEEDED(cfguInit())) {
        Result r = CFGU_GetSystemLanguage(&sysLanguage);
        cfguExit();
        if (R_SUCCEEDED(r)) {
            switch (sysLanguage) {
            case CFG_LANGUAGE_DE: return strings::Lang::DE;
            case CFG_LANGUAGE_FR: return strings::Lang::FR;
            case CFG_LANGUAGE_IT: return strings::Lang::IT;
            case CFG_LANGUAGE_ES: return strings::Lang::ES;
            default: break;
            }
        }
    }
    return strings::Lang::EN;
}

void applyLanguage(const Prefs &prefs) {
    strings::setLanguage(resolveLanguage(prefs));
}

void drawSettingsScreen(C2D_TextBuf textBuf, const ui::Touch &touch, Prefs *prefs, BottomScreenState *screenState) {
    ui::drawText(textBuf, strings::kSettings, 8, 8, 0.55f, ui::kColorText);

    ui::drawText(textBuf, strings::kSettingsBottomScreenVideo, 8, 44, 0.45f, ui::kColorText);
    ui::Rect t3 { 250, 40, 60, 28 };
    if (ui::toggle(touch, t3, prefs->bottomScreenVideo)) {
        prefs->bottomScreenVideo = !prefs->bottomScreenVideo;
        prefs->save();
    }
    // Only affects single-screen stream types (GC_GBA_LINK today) -- see
    // this file's own top comment and useBottomForVideo in main(). One
    // line (not the old two-line split) since the hint text is now a
    // single central string (strings::kSettingsBottomScreenVideoHint) --
    // a smaller scale than the two-line version used keeps it fitting the
    // 320px-wide bottom screen.
    ui::drawText(textBuf, strings::kSettingsBottomScreenVideoHint, 8, 72, 0.32f, ui::kColorTextDim);

    // Opens LANGUAGE (a plain list, see drawLanguageScreen()) instead of
    // cycling in place -- same "tap the row, pick from a list" flow as
    // every other client now uses. Fits in the gap between the hint above
    // and the antialiasing header below rather than claiming a whole new
    // row of its own -- the 240px-tall bottom screen is already tight (see
    // backRect below).
    ui::drawText(textBuf, strings::kSettingsLanguage, 8, 84, 0.4f, ui::kColorTextDim);
    const char *languageLabel = prefs->language == Prefs::LanguagePref::DE ? strings::kLanguageGerman
                               : prefs->language == Prefs::LanguagePref::EN ? strings::kLanguageEnglish
                                                                             : strings::kLanguageSystem;
    ui::Rect languageRect { 200, 80, 104, 20 };
    if (ui::button(textBuf, touch, languageRect, languageLabel)) {
        *screenState = BottomScreenState::LANGUAGE;
    }

    // Same "tap the row, pick from a list" pattern as language above,
    // squeezed into the same tight-space discipline (see this function's
    // own comment on languageRect) -- one more row than this screen had
    // room for before, so the antialiasing header/list/backRect below are
    // all shifted down and slightly compressed (18px step instead of 22,
    // 16px toggle height instead of 20) to still fit the 240px-tall bottom
    // screen. Worth a real on-device visual check -- this was tuned by
    // arithmetic, not by actually seeing it rendered.
    ui::drawText(textBuf, strings::kSettingsVideoMode, 8, 108, 0.4f, ui::kColorTextDim);
    const char *videoModeLabel = prefs->videoMode == "h264" ? strings::kVideoModeH264
                                : prefs->videoMode == "h265" ? strings::kVideoModeH265
                                : prefs->videoMode == "legacy" ? strings::kVideoModeLegacy
                                                                : strings::kVideoModeTiles;
    ui::Rect videoModeRect { 200, 104, 104, 18 };
    if (ui::button(textBuf, touch, videoModeRect, videoModeLabel)) {
        *screenState = BottomScreenState::VIDEO_MODE;
    }

    // Antialiasing (bilinear vs. nearest-neighbor upscale), configured per
    // stream_type rather than one global toggle -- GBA/DS pixel art and a
    // Wii U GamePad's higher-effective-resolution render suit different
    // filtering. Listed here (docs/protocol.md's fixed set of stream
    // types) rather than only offering "the current one": this screen is
    // only ever reachable pre-connect (see this file's own top comment),
    // so there's no single "current" stream_type to key off anyway --
    // whichever type is actually connected to later just reads whatever
    // was configured here in advance (see main()'s onConnected handling).
    ui::drawText(textBuf, strings::kSettingsAntialiasing, 8, 128, 0.4f, ui::kColorTextDim);
    float y = 146.0f;
    for (const auto &entry : kKnownStreamTypes) {
        ui::drawText(textBuf, labelForStreamType(entry.streamType), 8, y + 3, 0.38f, ui::kColorText);
        ui::Rect r { 250, y, 60, 16 };
        bool bilinear = prefs->bilinearFor(entry.streamType);
        if (ui::toggle(touch, r, bilinear)) {
            prefs->setBilinearFor(entry.streamType, !bilinear);
            prefs->save();
        }
        y += 18.0f;
    }

    ui::Rect backRect { 8, 220, 304, 18 };
    if (ui::button(textBuf, touch, backRect, strings::kBack)) {
        *screenState = BottomScreenState::MENU;
    }
}

// Plain list, same "tap a row, pick from it, land back where you were"
// flow as every other client's language screen -- picking one applies it
// immediately (every strings::kFoo read this frame onward already reflects
// it, being a plain immediate-mode redraw) and returns to SETTINGS.
void drawLanguageScreen(C2D_TextBuf textBuf, const ui::Touch &touch, Prefs *prefs, BottomScreenState *screenState) {
    ui::drawText(textBuf, strings::kSettingsLanguage, 8, 8, 0.55f, ui::kColorText);

    struct LanguageOption {
        Prefs::LanguagePref pref;
        const char *label;
    };
    LanguageOption options[] = {
        { Prefs::LanguagePref::SYSTEM, strings::kLanguageSystem },
        { Prefs::LanguagePref::DE, strings::kLanguageGerman },
        { Prefs::LanguagePref::EN, strings::kLanguageEnglish },
        { Prefs::LanguagePref::FR, strings::kLanguageFrench },
        { Prefs::LanguagePref::IT, strings::kLanguageItalian },
        { Prefs::LanguagePref::ES, strings::kLanguageSpanish },
    };
    // Sorted by the displayed label, not a fixed order -- "System" is
    // localized like any other UI string, but every language name itself
    // is a fixed endonym (see strings.json), so this only actually
    // reorders relative to "System"/"Système"/... as more languages are
    // added later.
    std::sort(std::begin(options), std::end(options),
        [](const LanguageOption &a, const LanguageOption &b) { return strcmp(a.label, b.label) < 0; });
    // Smaller row height/step than a 3-option list would need -- six rows
    // have to fit in the 240px-tall bottom screen alongside the title.
    float y = 40.0f;
    for (const auto &option : options) {
        ui::Rect r { 8, y, 304, 26 };
        if (ui::button(textBuf, touch, r, option.label)) {
            prefs->language = option.pref;
            prefs->save();
            applyLanguage(*prefs);
            *screenState = BottomScreenState::SETTINGS;
        }
        y += 30.0f;
    }
}

// Same "tap a row, pick from it, land back where you were" flow as
// drawLanguageScreen() above, cloned wholesale -- only 4 options, so this
// fits comfortably without needing that screen's tight-spacing tricks.
void drawVideoModeScreen(C2D_TextBuf textBuf, const ui::Touch &touch, Prefs *prefs, BottomScreenState *screenState) {
    ui::drawText(textBuf, strings::kSettingsVideoMode, 8, 8, 0.55f, ui::kColorText);

    struct VideoModeOption {
        const char *value; // wire-format string, see finlink/docs/protocol.md
        const char *label;
    };
    // NOT sorted alphabetically, unlike the language list above --
    // deliberate order (default first, then the two stronger/lossier
    // compressed options, legacy last as the explicit-opt-out fallback),
    // same as Android's Prefs.VIDEO_MODES.
    VideoModeOption options[] = {
        { "tiles", strings::kVideoModeTiles },
        { "h264", strings::kVideoModeH264 },
        { "h265", strings::kVideoModeH265 },
        { "legacy", strings::kVideoModeLegacy },
    };
    float y = 40.0f;
    for (const auto &option : options) {
        ui::Rect r { 8, y, 304, 26 };
        if (ui::button(textBuf, touch, r, option.label)) {
            prefs->videoMode = option.value;
            prefs->save();
            *screenState = BottomScreenState::SETTINGS;
        }
        y += 30.0f;
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

    discovery::BeaconListener beaconListener;
    beaconListener.start();

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    C3D_RenderTarget *topTarget = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bottomTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    C2D_TextBuf textBuf = C2D_TextBufNew(4096);

    Prefs prefs;
    applyLanguage(prefs);
    MenuState menu;
    GbaSession session;
    VideoTex videoTex;
    AudioPlayer audio;
    // No bilinear filter applied here -- unlike the single old global
    // toggle, the preference is per-stream-type now, and no stream_type is
    // known until a connection's onConnected fires (see
    // filterAppliedThisSession below). VideoTex's own constructor already
    // defaults its texture to nearest-neighbor, so there's nothing to set
    // yet regardless.

    BottomScreenState screenState = BottomScreenState::MENU;
    // Written from the session's background thread (onConnected/
    // onDisconnected callbacks) and read every frame on the main thread.
    std::atomic<bool> connected { false };
    std::string connectedHost;
    std::string connectedStreamType;
    // session_ready.video_mode, written from onConnected same as
    // connectedStreamType above (before *connected=true). Compared against
    // prefs.videoMode below to show a fallback prompt -- safe to read
    // prefs.videoMode directly at render time rather than snapshotting a
    // separate "requested" copy, since Settings (the only place it can
    // change) is unreachable while connected (see the dispatch chain
    // below), so it can't change out from under this comparison mid-session.
    std::string connectedGrantedVideoMode;
    uint16_t physicalMask = 0;
    ui::Touch touch;
    int exitHoldTicks = 0;
    // Set once per connection, right after applying that connection's
    // stream_type's filter preference -- videoTex->setBilinearFilter()
    // touches the live C3D_Tex (C3D_TexSetFilter), so it must run on this
    // (the main/render) thread, same discipline as every other GPU call in
    // this file; onConnected itself only runs on the session's background
    // thread and must not call it directly (see session.hpp).
    bool filterAppliedThisSession = false;
    // Main-thread-only (unlike the atomics/strings above, this is only
    // ever touched by the render loop and the "Fortsetzen" button below) --
    // true once the user has dismissed the video-mode fallback prompt for
    // the current connection, so it doesn't reappear every frame for the
    // rest of the session.
    bool videoModeFallbackAcknowledged = false;

    while (aptMainLoop()) {
        hidScanInput();
        touchPosition rawTouch;
        hidTouchRead(&rawTouch);
        touch.wasDown = touch.down;
        touch.down = (hidKeysHeld() & KEY_TOUCH) != 0;
        touch.x = rawTouch.px;
        touch.y = rawTouch.py;

        if (connected) {
            if (!filterAppliedThisSession) {
                videoTex.setBilinearFilter(prefs.bilinearFor(connectedStreamType));
                filterAppliedThisSession = true;
            }

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
            filterAppliedThisSession = false;
            exitHoldTicks = 0;
            videoModeFallbackAcknowledged = false;
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
            connected && shouldShowVideoOnBottomScreen(prefs.bottomScreenVideo, connectedStreamType);
        C3D_RenderTarget *videoTarget = useBottomForVideo ? bottomTarget : topTarget;
        C3D_RenderTarget *uiTarget = useBottomForVideo ? topTarget : bottomTarget;
        const float videoTargetWidth = useBottomForVideo ? 320.0f : 400.0f;

        C2D_TargetClear(videoTarget, ui::kColorBg);
        C2D_SceneBegin(videoTarget);
        if (connected && videoTex.hasFrame()) {
            videoTex.drawFitted(0, 0, videoTargetWidth, 240);
        } else {
            ui::drawText(textBuf, strings::kAppName, 150, 100, 0.9f, ui::kColorText);
            if (connected) {
                ui::drawText(textBuf, strings::kWaitingForImage, 130, 140, 0.5f, ui::kColorTextDim);
            }
        }

        C2D_TargetClear(uiTarget, ui::kColorBg);
        C2D_SceneBegin(uiTarget);

        // Empty connectedGrantedVideoMode means the server predates
        // session_ready.video_mode entirely -- skip the comparison rather
        // than assuming "tiles" was granted, see docs/protocol.md
        // "Video-mode fallback" and connectedGrantedVideoMode's own comment.
        const bool showVideoModeFallback = connected && !videoModeFallbackAcknowledged &&
            !connectedGrantedVideoMode.empty() && connectedGrantedVideoMode != prefs.videoMode;

        if (showVideoModeFallback) {
            // Non-blocking in spirit (the stream keeps playing/receiving
            // input underneath, session.sendInput() below still runs) --
            // this just occupies the status area other clients would use
            // for the same heads-up, since this UI has nowhere else to put
            // an overlay.
            ui::drawText(textBuf, strings::kVideoModeFallbackTitle, 8, 8, 0.5f, ui::kColorText);
            char message[256];
            const char *requestedLabel = prefs.videoMode == "h264" ? strings::kVideoModeH264
                                        : prefs.videoMode == "h265" ? strings::kVideoModeH265
                                        : prefs.videoMode == "legacy" ? strings::kVideoModeLegacy
                                                                       : strings::kVideoModeTiles;
            const char *grantedLabel = connectedGrantedVideoMode == "h264" ? strings::kVideoModeH264
                                      : connectedGrantedVideoMode == "h265" ? strings::kVideoModeH265
                                      : connectedGrantedVideoMode == "legacy" ? strings::kVideoModeLegacy
                                                                               : strings::kVideoModeTiles;
            snprintf(message, sizeof(message), strings::kVideoModeFallbackMessage, requestedLabel, grantedLabel);
            ui::drawText(textBuf, message, 8, 40, 0.4f, ui::kColorTextDim);
            ui::Rect continueRect { 8, 100, 304, 26 };
            if (ui::button(textBuf, touch, continueRect, strings::kVideoModeFallbackContinue)) {
                videoModeFallbackAcknowledged = true;
            }
            ui::Rect abortRect { 8, 134, 304, 26 };
            if (ui::button(textBuf, touch, abortRect, strings::kVideoModeFallbackAbort)) {
                session.disconnect();
                connected = false;
            }
            session.sendInput(physicalMask);
        } else if (connected) {
            ui::drawText(textBuf, strings::kStatusConnected, 8, 90, 0.55f, ui::kColorText);
            // Temporary debug line while tracking down the garbled-video
            // report on Cemu's WIIU_GAMEPAD stream (854x480, never
            // exercised by this client before) -- shows exactly what
            // width/height made it through decode, to tell a client-side
            // upload/draw bug apart from a server/negotiation mismatch.
            char dbg[48];
            snprintf(dbg, sizeof(dbg), "Debug: %ux%u (Stream: %s)", videoTex.debugFrameWidth(),
                      videoTex.debugFrameHeight(), connectedStreamType.c_str());
            ui::drawText(textBuf, dbg, 8, 108, 0.36f, ui::kColorButtonHeld);
            ui::drawText(textBuf, strings::kStatusPhysicalInputActive, 8, 130, 0.42f, ui::kColorTextDim);
            ui::drawText(textBuf, strings::kExitHoldHint, 8, 150, 0.42f, ui::kColorTextDim);
            ui::Rect disconnectRect { 8, 206, 304, 26 };
            if (ui::button(textBuf, touch, disconnectRect, strings::kDisconnect)) {
                session.disconnect();
                connected = false;
            }
            session.sendInput(physicalMask);
        } else if (screenState == BottomScreenState::MENU) {
            drawMenuScreen(textBuf, touch, &menu, &screenState, &session, &videoTex, &audio, &connected,
                           &connectedHost, &connectedStreamType, &prefs, &connectedGrantedVideoMode,
                           &beaconListener);
        } else if (screenState == BottomScreenState::SETTINGS) {
            drawSettingsScreen(textBuf, touch, &prefs, &screenState);
        } else if (screenState == BottomScreenState::VIDEO_MODE) {
            drawVideoModeScreen(textBuf, touch, &prefs, &screenState);
        } else {
            drawLanguageScreen(textBuf, touch, &prefs, &screenState);
        }

        C3D_FrameEnd(0);

        // promptForHost() draws its own frames via the swkbd applet, so it
        // must run here, outside C3D_FrameBegin/End, not from inside
        // drawMenuScreen() where the tap that requests it is detected.
        bool wantsHostPrompt;
        std::string currentHostText;
        {
            std::lock_guard<std::mutex> lock(menu.mutex);
            wantsHostPrompt = menu.hostPromptRequested;
            menu.hostPromptRequested = false;
            currentHostText = menu.hostText;
        }
        if (wantsHostPrompt) {
            std::string newHost = promptForHost(currentHostText);
            std::lock_guard<std::mutex> lock(menu.mutex);
            menu.hostText = newHost;
        }
    }

    session.disconnect();
    beaconListener.stop();
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
