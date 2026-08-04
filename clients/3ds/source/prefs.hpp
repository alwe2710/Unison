#pragma once

#include <map>
#include <string>

// Settings, mirroring clients/switch/source/prefs.hpp minus key bindings
// and the on-screen-controls toggle (see gba_buttons.hpp resp. main.cpp:
// the 3DS's physical buttons already match the GBA's layout and its
// bottom screen is fully occupied by Menu/Settings/status instead of a
// touch overlay, so neither concept applies here). Persisted as a flat
// key=value text file on the SD card.
class Prefs {
  public:
    Prefs();

    void save();

    // Per-stream-type ("GC_GBA_LINK", "WIIU_GAMEPAD", ...) bilinear filter
    // preference -- true = bilinear filtering (smooth upscale), false =
    // nearest-neighbor (crisp/pixelated upscale). Not one global toggle:
    // GBA/DS pixel art and a Wii U GamePad's higher-effective-resolution
    // render suit different filtering. Configured in advance from
    // drawSettingsScreen()'s per-console list (this screen is only ever
    // shown pre-connect, before any stream_type is known) and applied once
    // connected, see main()'s filterAppliedThisSession. A type not yet
    // explicitly set falls back to a per-type default (prefs.cpp's
    // defaultBilinearFor()): nearest for GC_GBA_LINK, bilinear for
    // WIIU_GAMEPAD/N3DS_BOTTOM_SCREEN/NDS_BOTTOM_SCREEN.
    bool bilinearFor(const std::string &streamType) const;
    void setBilinearFor(const std::string &streamType, bool value);

    // true = show the stream on the bottom screen instead of the top one
    // (default). Only actually consulted for a single-screen stream_type
    // (GC_GBA_LINK today) -- a stream_type that's itself a dual-screen
    // source's own secondary screen (docs/protocol.md "Stream-Typen")
    // always goes to the bottom screen regardless, see main.cpp and
    // shouldShowVideoOnBottomScreen() below.
    bool bottomScreenVideo = false;

    // SYSTEM (default) resolves to strings::Lang::DE only if the 3DS's own
    // system language (CFGU_GetSystemLanguage, read once at startup in
    // main.cpp -- prefs.cpp itself stays free of 3ds.h/cfgu so it can stay
    // a plain, testable key=value file reader/writer) is German; DE/EN are
    // an explicit override from the Settings screen's language button.
    enum class LanguagePref { SYSTEM, DE, EN, FR, IT, ES };
    LanguagePref language = LanguagePref::SYSTEM;

    // Sent verbatim as hello_ack.video_mode during the handshake (see
    // session.cpp's performAppHandshake()) -- one of the wire-format
    // strings finlink's docs/protocol.md defines ("tiles"/"legacy"/"h264"/
    // "h265"), not a client-side enum, same as Android's Prefs.videoMode.
    std::string videoMode = "tiles";

  private:
    void load();

    std::map<std::string, bool> bilinearVideoFilterByStreamType;
};

// The screen-choice decision main.cpp's render loop consults every frame --
// pulled out into its own free function (rather than left as the inline
// `bottomScreenVideo || finlink_stream_type_prefers_secondary_screen(...)`
// expression it used to be) so it has one place to unit-test (see
// tests/test_dual_screen_choice.cpp) instead of only being exercisable by
// running the real render loop on hardware/citra. Not a Prefs method: it
// only reads the one pref field, doesn't need the rest of Prefs' lifecycle
// (file I/O, bilinear map, ...) to be constructed just to call it.
//
// bottomScreenVideoPref is the user's own top/bottom choice
// (Prefs::bottomScreenVideo) -- only actually consulted for a single-screen
// stream_type; a dual-screen source's own secondary screen always forces
// bottom regardless, see finlink_stream_type_prefers_secondary_screen()
// (finlink/handshake.h).
bool shouldShowVideoOnBottomScreen(bool bottomScreenVideoPref, const std::string &streamType);
