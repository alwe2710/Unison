# clients/3ds

Nintendo 3DS homebrew client (`.3dsx`, devkitARM/libctru + citro2d/citro3d).
Uses the console's two screens directly instead of the
Menu/Settings/Player screen stack the other clients use: **top screen**
always shows the GBA stream (or an idle "Unison" screen before
connecting), **bottom screen** shows Menu/Settings before connecting and
the on-screen touch controls (+ a "Trennen" button) while playing.

## Architecture

All protocol/transport logic comes unmodified from [`../../core/`](../../core/).
There's no UI framework like borealis (Switch) here -- citro2d only offers
basic primitives (rectangles, text, images), no ready-made widgets, so every
menu/button is hand-rolled (`ui.hpp`), in the same style as the Switch
client's on-screen touch overlay (`clients/switch/source/video_view.cpp`).

- **`source/session.{hpp,cpp}`** -- WS handshake/framing + session loop on
  a background thread, practically identical to
  `clients/switch/source/session.cpp` (portable C++, only the RNG source
  differs: `rand()` instead of `randomGet()`, since devkitARM has the same
  missing `getentropy()` binding as devkitA64).
- **`source/discovery.{hpp,cpp}`** -- UDP discovery-beacon listener
  (`BeaconListener`, port 6805, see `docs/protocol.md`
  "Discovery-Beacon (UDP)") on its own background thread, plus `/status`
  polling for slot occupancy after server selection. `gethostid()` now only
  supplies the client's own IP for the diagnostic display.
- **`source/video_tex.{hpp,cpp}`** -- GBA video as a citro3d texture.
  RGB565 is uploaded directly (no RGBA8 conversion like the Switch client's
  NanoVG path -- GPU_RGB565 matches the wire format exactly, which matters
  on the 3DS's much weaker CPU). The texture is fixed at 256x256 (PICA200
  needs powers of two); only the 240x160 sub-region is drawn, via a
  `Tex3DS_SubTexture`.
- **`source/audio.{hpp,cpp}`** -- audio playback via NDSP. Unlike the
  Switch client (whose `audout` device is fixed at 48kHz/stereo and
  therefore has to resample), NDSP accepts an arbitrary rate via
  `ndspChnSetRate()` -- no resampling needed.
- **`source/gba_buttons.hpp`** -- no key rebinding like Android/Switch:
  the 3DS's button layout (D-pad, A/B, L/R, Start/Select) already matches
  the GBA's almost 1:1, so there's just a fixed default mapping.
- **`source/ui.hpp`** -- rectangles/buttons/toggles + manual touch
  hit-testing for Menu/Settings/on-screen controls/the language picker
  (see [Localization](#localization) below).

## Localization

The Settings screen's "Sprache"/"Language" row opens its own
`BottomScreenState::LANGUAGE` screen (`drawLanguageScreen()` in
`main.cpp`), an alphabetically-sorted list (System, plus every language in
[`i18n/strings.json`](../../i18n/strings.json)) drawn with the same
`ui::button()` rows as everywhere else -- tapping one sets `prefs->language`,
saves it, and calls `applyLanguage()`, which re-resolves and calls
`strings::setLanguage()`. Since this is a plain immediate-mode redraw (not
a retained view tree), every string on screen already reflects the new
language the very next frame, no extra refresh logic needed.
`resolveLanguage()` detects the console's own system language via
`CFGU_GetSystemLanguage()`, falling back to English if that call fails or
the system language isn't one of the five supported.

## Building

Requires devkitPro with `devkitARM`, `libctru`, `citro2d`, `citro3d`,
`3dstools`, `general-tools`, `3ds-cmake`, `devkitarm-cmake`, and
`3ds-pkg-config` under `$DEVKITPRO`.

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$DEVKITPRO/portlibs/3ds/bin:$PATH

cmake -S clients/3ds -B clients/3ds/build -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build clients/3ds/build
```

Output: `clients/3ds/build/unison-3ds.3dsx`. Copy it to the SD card at
`/3ds/unison-3ds.3dsx` and launch it through the Homebrew Launcher.

## Status

- [x] Toolchain bootstrap verified (hello-world smoke test: compiles,
      links, produces a runnable `.3dsx`).
- [x] Menu (host entry via software keyboard, P1-P4 picker, LAN discovery
      via `gethostid()`, Settings link) -- bottom screen
- [x] Settings (on-screen-controls toggle, video filter toggle, language
      picker) -- bottom screen
- [x] Player -- video on the top screen, on-screen controls + physical
      buttons + "Trennen" on the bottom screen

Everything above compiles and links cleanly to a `.3dsx`/`.cia`, and has
since been installed and run on a real console via FBI. That first real
hardware pass found four issues, addressed as follows:

- Settings screen was cramped/hard to read (a purely arithmetic-tuned
  layout, never actually seen rendered before that pass) -- redesigned as
  a consistently-spaced navigation list, and Antialiasing split out into
  its own `BottomScreenState::ANTIALIASING` sub-screen (matching Android's
  separate `AntialiasingActivity`), same as Language/Video mode already
  had.
- Manual host entry always ran the GC_GBA_LINK P1-P4 lobby probe, even
  though the field's own placeholder text ("IP address or IP:port") always
  promised `host:port` would connect directly to a single-slot server the
  same way Android's manual entry does -- a `host:port` value fed straight
  into `inet_pton()` for all four probe ports, which fails identically on
  every one of them (see `source/host_port.{hpp,cpp}` and its test). Fixed
  by actually implementing that split, matching Android.
- A cosmetic rename leftover (`ProductCode: CTR-P-FNLK` in `cia/template.rsf`,
  from the pre-rename `finlink` naming) was found and fixed while
  investigating why the CIA didn't show up on the Home Menu after an FBI
  install -- the SMDH itself was independently verified correct (parsed by
  hand: title/publisher strings read "Unison" in every language, and the
  `Visible` flag is set), so this wasn't the actual cause of that symptom;
  it remains open, most likely a device-side icon-cache/rescan quirk or a
  conflicting leftover install under an old title, neither reproducible
  without hardware.
- Reported input lag/repeated taps needed during a beacon search was
  traced to `drawMenuScreen()` calling `gethostid()` (a real IPC round trip
  to the `soc:u` sysmodule, not a cheap local read) unconditionally on
  every single frame just to show the "your IP" diagnostic line -- now
  cached and refreshed only every 2s. `BeaconListener` and the P1-P4 probe
  both already run on genuine background `std::thread`s (not literally
  blocking the main thread), so if lag is still reported after this fix,
  the next suspect is 3DS-specific thread scheduling/priority contention
  on a single physical core, which would need `threadCreate()`-level
  priority tuning -- not yet attempted, since it's speculative without
  hardware to confirm it against.

That pass didn't get as far as a successful stream connection (see the
`host:port` bug above), so the video texture UV convention
(`Tex3DS_SubTexture`) and NDSP audio playback are still unconfirmed on
real hardware -- next real-hardware pass, now that manual entry actually
works, should cover that.
