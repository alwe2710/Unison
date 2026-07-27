#pragma once

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

    // true = bilinear filtering (smooth upscale), false = nearest-neighbor
    // (crisp/pixelated upscale, the default) -- same rationale as the
    // other clients' toggle.
    bool bilinearVideoFilter = false;

    // true = show the stream on the bottom screen instead of the top one
    // (default). Only actually consulted for a single-screen stream_type
    // (GC_GBA_LINK today) -- a stream_type that's itself a dual-screen
    // source's own secondary screen (docs/protocol.md "Stream-Typen")
    // always goes to the bottom screen regardless, see main.cpp and
    // finlink_stream_type_prefers_secondary_screen() (finlink/handshake.h).
    bool bottomScreenVideo = false;

  private:
    void load();
};
