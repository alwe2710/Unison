// Unison for Nintendo 3DS: by default, top screen shows the GBA stream,
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
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <3ds.h>
#include <citro2d.h>

#include "unison/handshake.h"

#include "audio.hpp"
#include "discovery.hpp"
#include "gba_buttons.hpp"
#include "h264_decoder.hpp"
#include "host_port.hpp"
#include "language_pref.hpp"
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
// Alphabetical by displayed label (3DS, GBA/GC, NDS, Wii U), per explicit
// request -- a fixed order rather than a dynamic sort, since all four
// console names are untranslated technical/brand terms, identical across
// every language (see i18n/strings.json), unlike drawLanguageScreen()'s
// own endonym-based sort elsewhere.
constexpr StreamTypeEntry kKnownStreamTypes[] = {
    { "N3DS_BOTTOM_SCREEN" },
    { "GC_GBA_LINK" },
    { "NDS_BOTTOM_SCREEN" },
    { "WIIU_GAMEPAD" },
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

enum class BottomScreenState { MENU, SETTINGS, LANGUAGE, VIDEO_MODE, ANTIALIASING, CONSOLE_SETTINGS };

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
                     std::string *connectedGrantedVideoMode, discovery::BeaconListener *beaconListener,
                     std::unique_ptr<H264Decoder> *compressedVideoDecoder) {
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
        if (auto hp = splitHostPort(hostText)) {
            // "host:port" -- connect straight to that single-slot server,
            // same as tapping a non-GC_GBA_LINK beacon entry below does.
            // See splitHostPort()'s own comment for why this branch exists.
            *connectedHost = hp->host;
            videoTex->reset();
            compressedVideoDecoder->reset();
            // "" -- manual host:port entry, real stream_type unknown until
            // hello, see Prefs::videoModeFor()'s own comment.
            session->connect(hp->host, hp->port, prefs->videoModeFor(""),
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
                    .onCompressedVideoFrame =
                        [videoTex, compressedVideoDecoder](uint32_t w, uint32_t h, std::vector<uint8_t> data) {
                            // Built on first use (not eagerly in
                            // onConnected) since this is the first point
                            // the real coded width/height is known --
                            // H264Decoder's own input/output config is
                            // fixed for its whole lifetime, see that
                            // class's own comment.
                            if (!*compressedVideoDecoder) {
                                *compressedVideoDecoder = std::make_unique<H264Decoder>(w, h);
                            }
                            if (!(*compressedVideoDecoder)->isValid()) {
                                return;
                            }
                            std::vector<uint8_t> rgb565;
                            if ((*compressedVideoDecoder)->decode(data.data(), data.size(), rgb565)) {
                                videoTex->setFrame(w, h, rgb565);
                            }
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
        } else {
            runSearch(menu, hostText);
        }
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
                compressedVideoDecoder->reset();
                session->connect(lastSearchedHost, port, prefs->videoModeFor("GC_GBA_LINK"),
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
                        .onCompressedVideoFrame =
                            [videoTex, compressedVideoDecoder](uint32_t w, uint32_t h, std::vector<uint8_t> data) {
                                if (!*compressedVideoDecoder) {
                                    *compressedVideoDecoder = std::make_unique<H264Decoder>(w, h);
                                }
                                if (!(*compressedVideoDecoder)->isValid()) {
                                    return;
                                }
                                std::vector<uint8_t> rgb565;
                                if ((*compressedVideoDecoder)->decode(data.data(), data.size(), rgb565)) {
                                    videoTex->setFrame(w, h, rgb565);
                                }
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
    //
    // localIpString() -> gethostid() is a real IPC round-trip to the soc:u
    // sysmodule (like every libctru soc.h call), not a cheap local read --
    // calling it unconditionally every single frame (as this used to) adds
    // that IPC latency to every frame's budget on exactly the screen where
    // taps are reported as needing to be repeated. The local IP practically
    // never changes mid-session, so this is cached and only refreshed every
    // 2s instead of 60x/s.
    static std::string cachedMyIp;
    static std::chrono::steady_clock::time_point lastIpRefresh;
    auto nowSteady = std::chrono::steady_clock::now();
    if (lastIpRefresh.time_since_epoch().count() == 0 || nowSteady - lastIpRefresh > std::chrono::seconds(2)) {
        cachedMyIp = discovery::localIpString();
        lastIpRefresh = nowSteady;
    }
    std::string netLine;
    if (cachedMyIp.empty()) {
        netLine = strings::kDiscoveryNoNetwork;
    } else {
        char buf[80];
        snprintf(buf, sizeof(buf), strings::kDiscoveryOwnIp, cachedMyIp.c_str());
        netLine = buf;
    }
    ui::drawText(textBuf, netLine.c_str(), 190, 74, 0.38f, cachedMyIp.empty() ? ui::kColorButtonHeld : ui::kColorTextDim);

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
                compressedVideoDecoder->reset();
                session->connect(srv.host, srv.handshakePort, prefs->videoModeFor(srv.streamType),
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
                        .onCompressedVideoFrame =
                            [videoTex, compressedVideoDecoder](uint32_t w, uint32_t h, std::vector<uint8_t> data) {
                                if (!*compressedVideoDecoder) {
                                    *compressedVideoDecoder = std::make_unique<H264Decoder>(w, h);
                                }
                                if (!(*compressedVideoDecoder)->isValid()) {
                                    return;
                                }
                                std::vector<uint8_t> rgb565;
                                if ((*compressedVideoDecoder)->decode(data.data(), data.size(), rgb565)) {
                                    videoTex->setFrame(w, h, rgb565);
                                }
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
// from drawSettingsScreen()'s language button below. The actual pref/
// system-language resolution decision lives in resolveLanguagePref()
// (language_pref.hpp/.cpp) -- this just does the CFGU_GetSystemLanguage()
// call and translates its raw CFG_LANGUAGE_* result into strings::Lang.
strings::Lang resolveLanguage(const Prefs &prefs) {
    std::optional<strings::Lang> sysLanguage;
    u8 rawSysLanguage = 0;
    if (R_SUCCEEDED(cfguInit())) {
        Result r = CFGU_GetSystemLanguage(&rawSysLanguage);
        cfguExit();
        if (R_SUCCEEDED(r)) {
            switch (rawSysLanguage) {
            case CFG_LANGUAGE_DE: sysLanguage = strings::Lang::DE; break;
            case CFG_LANGUAGE_FR: sysLanguage = strings::Lang::FR; break;
            case CFG_LANGUAGE_IT: sysLanguage = strings::Lang::IT; break;
            case CFG_LANGUAGE_ES: sysLanguage = strings::Lang::ES; break;
            default: break;
            }
        }
    }
    return resolveLanguagePref(prefs.language, sysLanguage);
}

void applyLanguage(const Prefs &prefs) {
    strings::setLanguage(resolveLanguage(prefs));
}

// Redesigned as a consistently-spaced, full-width "tap anywhere in the row"
// navigation list (current value baked right into the row's own label) --
// closer to how Android's SettingsActivity reads, and replaces the old
// label-left/tiny-button-right layout that was tuned purely by arithmetic
// and never actually seen rendered on real hardware before now (see git
// history/this function's previous revision). Antialiasing moved out to
// its own sub-screen (drawAntialiasingScreen() below), matching Android's
// separate AntialiasingActivity and freeing up enough room here that every
// row gets the same 38px step instead of being squeezed to fit.
void drawSettingsScreen(C2D_TextBuf textBuf, const ui::Touch &touch, Prefs *prefs, BottomScreenState *screenState) {
    ui::drawText(textBuf, strings::kSettings, 8, 8, 0.55f, ui::kColorText);

    // Row 1: the one plain toggle on this screen (not a sub-screen -- a
    // single on/off doesn't need one). Only affects single-screen stream
    // types (GC_GBA_LINK today) -- see this file's own top comment and
    // useBottomForVideo in main().
    ui::drawText(textBuf, strings::kSettingsBottomScreenVideo, 8, 48, 0.42f, ui::kColorText);
    ui::Rect videoToggleRect { 250, 40, 62, 28 };
    if (ui::toggle(touch, videoToggleRect, prefs->bottomScreenVideo)) {
        prefs->bottomScreenVideo = !prefs->bottomScreenVideo;
        prefs->save();
    }
    ui::drawText(textBuf, strings::kSettingsBottomScreenVideoHint, 8, 74, 0.32f, ui::kColorTextDim);

    // Rows 2-4: full-width navigation buttons, each opening its own
    // sub-screen (same "tap the row, pick from a list, land back here"
    // flow every sub-screen already uses). The current value is part of
    // the button's own label, so there's nothing to read elsewhere on the
    // row -- the whole row is one tap target, not just a small control on
    // its right edge.
    //
    // (Previously this branch only recognized DE/EN and fell back to
    // "System" for FR/IT/ES, which was wrong -- the language actually
    // applied still matched prefs->language correctly, only this screen's
    // own label was misleading. Fixed here incidentally while rewriting.)
    const char *languageLabel = prefs->language == Prefs::LanguagePref::DE ? strings::kLanguageGerman
                               : prefs->language == Prefs::LanguagePref::EN ? strings::kLanguageEnglish
                               : prefs->language == Prefs::LanguagePref::FR ? strings::kLanguageFrench
                               : prefs->language == Prefs::LanguagePref::IT ? strings::kLanguageItalian
                               : prefs->language == Prefs::LanguagePref::ES ? strings::kLanguageSpanish
                                                                             : strings::kLanguageSystem;
    std::string languageRowLabel = std::string(strings::kSettingsLanguage) + ": " + languageLabel;
    ui::Rect languageRect { 8, 96, 304, 32 };
    if (ui::button(textBuf, touch, languageRect, languageRowLabel.c_str())) {
        *screenState = BottomScreenState::LANGUAGE;
    }

    ui::Rect consoleSettingsRect { 8, 134, 304, 32 };
    if (ui::button(textBuf, touch, consoleSettingsRect, strings::kSettingsConsoleSpecific)) {
        *screenState = BottomScreenState::CONSOLE_SETTINGS;
    }

    ui::Rect backRect { 8, 172, 304, 26 };
    if (ui::button(textBuf, touch, backRect, strings::kBack)) {
        *screenState = BottomScreenState::MENU;
    }
}

// Top of the "Konsolenspezifische Einstellungen" row (drawSettingsScreen)
// -- lists every stream_type docs/protocol.md defines (this screen is only
// ever reachable pre-connect, see this file's own top comment, so there's
// no single "current" stream_type to key off anyway), each a full-width nav
// row into drawAntialiasingScreen() (now the per-console detail screen)
// rather than the inline toggle this used to be -- see that function's own
// comment for why. selectedStreamType is *this* function's caller-owned
// output, not read here.
void drawConsoleSettingsScreen(C2D_TextBuf textBuf, const ui::Touch &touch, BottomScreenState *screenState,
                                std::string *selectedStreamType) {
    ui::drawText(textBuf, strings::kSettingsConsoleSpecific, 8, 8, 0.55f, ui::kColorText);

    // Plain console name only, no video-mode subtitle here (an earlier
    // revision showed one, reverted per explicit request) -- the video
    // mode itself is only ever shown/changed one screen further in, on
    // drawAntialiasingScreen().
    float y = 40.0f;
    for (const auto &entry : kKnownStreamTypes) {
        ui::Rect r { 8, y, 304, 32 };
        if (ui::button(textBuf, touch, r, labelForStreamType(entry.streamType))) {
            *selectedStreamType = entry.streamType;
            *screenState = BottomScreenState::ANTIALIASING;
        }
        y += 36.0f;
    }

    ui::Rect backRect { 8, y + 4, 304, 18 };
    if (ui::button(textBuf, touch, backRect, strings::kBack)) {
        *screenState = BottomScreenState::SETTINGS;
    }
}

// Per-console detail screen, reached from drawConsoleSettingsScreen()'s
// list above -- used to be that list itself (one row per stream_type, an
// inline bilinear toggle each and nothing else); moved out so this screen
// could show more than a single toggle per console without cramming both
// the antialiasing switch and a video-mode picker into one flat list row.
// Two settings live here, both keyed by streamType (drawn from the caller's
// selectedStreamType, set just before entering this state): bilinear-vs-
// nearest upscale (unchanged from before -- whatever's configured here is
// applied once connected, see main()'s onConnected handling) and the
// video-mode/compression picker (own sub-screen, drawVideoModeScreen) --
// used to be a single global choice shared by every console
// (drawSettingsScreen's own former "Videomodus" row), moved here per
// console for the same reason antialiasing already was.
void drawAntialiasingScreen(C2D_TextBuf textBuf, const ui::Touch &touch, Prefs *prefs, BottomScreenState *screenState,
                             const std::string &streamType) {
    ui::drawText(textBuf, labelForStreamType(streamType.c_str()), 8, 8, 0.55f, ui::kColorText);

    ui::drawText(textBuf, strings::kSettingsAntialiasing, 8, 48, 0.42f, ui::kColorText);
    ui::Rect toggleRect { 250, 40, 62, 28 };
    bool bilinear = prefs->bilinearFor(streamType);
    if (ui::toggle(touch, toggleRect, bilinear)) {
        prefs->setBilinearFor(streamType, !bilinear);
        prefs->save();
    }

    // No "h265" case -- see drawVideoModeScreen()'s own comment on why
    // this client never offers it; a stale "h265" saved by some earlier
    // build just falls through to the "tiles" default here, same as any
    // other unrecognized value already would.
    const char *videoModeLabel = prefs->videoModeFor(streamType) == "h264" ? strings::kVideoModeH264
                                : prefs->videoModeFor(streamType) == "legacy" ? strings::kVideoModeLegacy
                                                                              : strings::kVideoModeTiles;
    std::string videoModeRowLabel = std::string(strings::kSettingsVideoMode) + ": " + videoModeLabel;
    ui::Rect videoModeRect { 8, 86, 304, 32 };
    if (ui::button(textBuf, touch, videoModeRect, videoModeRowLabel.c_str())) {
        *screenState = BottomScreenState::VIDEO_MODE;
    }

    ui::Rect backRect { 8, 220, 304, 18 };
    if (ui::button(textBuf, touch, backRect, strings::kBack)) {
        *screenState = BottomScreenState::CONSOLE_SETTINGS;
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
// Per-console (streamType, always entered from drawAntialiasingScreen()'s
// detail screen now) rather than one global choice -- see
// Prefs::videoModeFor()'s own comment on why (picking H.264 for Cemu used
// to silently also request it from Dolphin next time, which never honors
// it anyway, but still).
void drawVideoModeScreen(C2D_TextBuf textBuf, const ui::Touch &touch, Prefs *prefs, BottomScreenState *screenState,
                          const std::string &streamType) {
    ui::drawText(textBuf, strings::kSettingsVideoMode, 8, 8, 0.55f, ui::kColorText);

    struct VideoModeOption {
        const char *value; // wire-format string, see unison/docs/protocol.md
        const char *label;
    };
    // NOT sorted alphabetically, unlike the language list above --
    // deliberate order, per explicit request: the two raw-deflate modes
    // first (legacy/"Raw (Deflate)" before tiles/"Raw+Tiling (Deflate)"),
    // then h264, same as Android's Prefs.VIDEO_MODES. No h265 entry here
    // at all, unlike every other client -- this one's own H264Decoder
    // (MVD, New3DS-exclusive hardware) never supported H.265 in the first
    // place (the 3DS predates HEVC entirely), so offering it would just
    // request a mode this client can never actually decode.
    VideoModeOption options[] = {
        { "legacy", strings::kVideoModeLegacy },
        { "tiles", strings::kVideoModeTiles },
        { "h264", strings::kVideoModeH264 },
    };
    float y = 40.0f;
    for (const auto &option : options) {
        ui::Rect r { 8, y, 304, 26 };
        if (ui::button(textBuf, touch, r, option.label)) {
            prefs->setVideoModeFor(streamType, option.value);
            prefs->save();
            *screenState = BottomScreenState::ANTIALIASING;
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
    // Same "one long-lived object shared across every connection this app
    // makes, reset() on each new connect" lifetime as videoTex itself --
    // null (not yet built) until the first h264 message of a session that
    // actually negotiated it arrives, see the .onCompressedVideoFrame
    // handlers' own comment.
    std::unique_ptr<H264Decoder> compressedVideoDecoder;
    AudioPlayer audio;
    // No bilinear filter applied here -- unlike the single old global
    // toggle, the preference is per-stream-type now, and no stream_type is
    // known until a connection's onConnected fires (see
    // filterAppliedThisSession below). VideoTex's own constructor already
    // defaults its texture to nearest-neighbor, so there's nothing to set
    // yet regardless.

    BottomScreenState screenState = BottomScreenState::MENU;
    // Which console CONSOLE_SETTINGS was last tapped for -- consulted by
    // ANTIALIASING (now the per-console detail screen) and VIDEO_MODE, both
    // reached only via that list. See drawConsoleSettingsScreen()'s own
    // comment.
    std::string selectedConsoleStreamType;
    // Written from the session's background thread (onConnected/
    // onDisconnected callbacks) and read every frame on the main thread.
    std::atomic<bool> connected { false };
    std::string connectedHost;
    std::string connectedStreamType;
    // session_ready.video_mode, written from onConnected same as
    // connectedStreamType above (before *connected=true). Compared against
    // prefs.videoModeFor(connectedStreamType) below to show a fallback
    // prompt -- safe to read that directly at render time rather than
    // snapshotting a separate "requested" copy, since Settings (the only
    // place it can change) is unreachable while connected (see the
    // dispatch chain below), so it can't change out from under this
    // comparison mid-session.
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
        // "Unison" placeholder) always stays on top and the UI on bottom,
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
            !connectedGrantedVideoMode.empty() && connectedGrantedVideoMode != prefs.videoModeFor(connectedStreamType);

        if (showVideoModeFallback) {
            // Non-blocking in spirit (the stream keeps playing/receiving
            // input underneath, session.sendInput() below still runs) --
            // this just occupies the status area other clients would use
            // for the same heads-up, since this UI has nowhere else to put
            // an overlay.
            ui::drawText(textBuf, strings::kVideoModeFallbackTitle, 8, 8, 0.5f, ui::kColorText);
            char message[256];
            const std::string requestedVideoMode = prefs.videoModeFor(connectedStreamType);
            const char *requestedLabel = requestedVideoMode == "h264" ? strings::kVideoModeH264
                                        : requestedVideoMode == "h265" ? strings::kVideoModeH265
                                        : requestedVideoMode == "legacy" ? strings::kVideoModeLegacy
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
                           &beaconListener, &compressedVideoDecoder);
        } else if (screenState == BottomScreenState::SETTINGS) {
            drawSettingsScreen(textBuf, touch, &prefs, &screenState);
        } else if (screenState == BottomScreenState::CONSOLE_SETTINGS) {
            drawConsoleSettingsScreen(textBuf, touch, &screenState, &selectedConsoleStreamType);
        } else if (screenState == BottomScreenState::VIDEO_MODE) {
            drawVideoModeScreen(textBuf, touch, &prefs, &screenState, selectedConsoleStreamType);
        } else if (screenState == BottomScreenState::ANTIALIASING) {
            drawAntialiasingScreen(textBuf, touch, &prefs, &screenState, selectedConsoleStreamType);
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
