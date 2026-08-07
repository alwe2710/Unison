#pragma once

#include <map>
#include <string>

#include "strings_generated.hpp"

// Settings, read/written from SettingsActivity and read by PlayerActivity.
// Persisted as a flat key=value text file on the SD card -- there's no
// SharedPreferences equivalent in libnx, and the one setting left here
// doesn't warrant pulling in a real config/JSON library.
//
// No on-screen-touch-overlay toggle and no per-GBA-button key rebinding
// here (unlike clients/android/.../Prefs.kt) -- both were removed at the
// user's request: every GBA button already has a sensible default
// physical-controller mapping (see gba_buttons.hpp), so neither concept
// pulled its weight on this platform.
class Prefs {
  public:
    Prefs();

    void save();

    // Per-stream-type ("GC_GBA_LINK", "WIIU_GAMEPAD", ...) bilinear filter
    // preference -- true = bilinear filtering (smooth upscale), false =
    // nearest-neighbor (crisp/pixelated upscale). Not one global toggle:
    // GBA/DS pixel art and a Wii U GamePad's higher-effective-resolution
    // render suit different filtering. A type not yet explicitly set
    // falls back to a per-type default (prefs.cpp's defaultBilinearFor()):
    // nearest for GC_GBA_LINK, bilinear for WIIU_GAMEPAD/
    // N3DS_BOTTOM_SCREEN/NDS_BOTTOM_SCREEN.
    bool bilinearFor(const std::string &streamType) const;
    void setBilinearFor(const std::string &streamType, bool value);

    // SYSTEM (default) resolves to strings::Lang::DE only if the console's
    // own system language (setGetSystemLanguage(), read via
    // resolveLanguage() below) is German; DE/EN are an explicit override
    // from the Settings screen's language cell.
    enum class LanguagePref { SYSTEM, DE, EN, FR, IT, ES };
    LanguagePref language = LanguagePref::SYSTEM;

    // Sent verbatim as hello_ack.video_mode during the handshake (see
    // session.cpp's performAppHandshake()) -- one of the wire-format
    // strings Unison's docs/protocol.md defines ("tiles"/"legacy"/"h264"/
    // "h265"), not a client-side enum, same as Android's
    // Prefs.videoModeFor(). Per stream_type, same reasoning/shape as
    // bilinearFor()/setBilinearFor() above -- used to be one single global
    // value shared by every console, which meant picking e.g. H.264 for
    // Cemu also silently requested H.264 the next time you connected to
    // Dolphin (which never honors it anyway, but still). "" (manual
    // host:port entry, real stream_type unknown until the handshake's
    // hello) falls back to kVideoModeDefault, same as any other
    // not-yet-configured console.
    std::string videoModeFor(const std::string &streamType) const;
    void setVideoModeFor(const std::string &streamType, const std::string &value);

    static constexpr const char *kVideoModeDefault = "tiles";

  private:
    std::map<std::string, std::string> values;

    void load();
    std::string path() const;
};

// Anything other than an explicit DE/EN override falls back to English,
// including the system-language query failing -- same policy as every
// other client. Defined here (not main.cpp) since both main.cpp (once, at
// startup) and settings_activity.cpp (whenever the user cycles the
// language cell) need it.
strings::Lang resolveLanguage(const Prefs &prefs);
void applyLanguage(const Prefs &prefs);
