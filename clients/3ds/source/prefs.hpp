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
    // finlink_stream_type_prefers_secondary_screen() (finlink/handshake.h).
    bool bottomScreenVideo = false;

  private:
    void load();

    std::map<std::string, bool> bilinearVideoFilterByStreamType;
};
