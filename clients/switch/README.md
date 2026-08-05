# clients/switch

Nintendo Switch Homebrew client (`.nro`, devkitA64/libnx). Same structure as
[`../android/`](../android/): **Menu** (host entry + network discovery + link
to Settings), **Settings** (on-screen-controls toggle, video filter toggle,
physical key bindings), **Player** (fullscreen video/audio/input). Native
"Horizon" UI look via [borealis](https://github.com/xfangfang/borealis).

## Architecture

All protocol/transport logic comes unmodified from [`../../core/`](../../core/)
(via `add_subdirectory`, see `CMakeLists.txt`) — this client only adds the
Switch-specific UI (borealis) and session orchestration.

- **`borealis/`** — vendored `library/` subdirectory of
  [xfangfang/borealis](https://github.com/xfangfang/borealis) (Apache 2.0),
  as plain source rather than a submodule, since only the Switch+glfw driver
  is needed. Borealis' own `glfw`/`SDL` submodules are only needed for its
  `PLATFORM_DESKTOP` build and aren't pulled in here.
- **`resources/`** — font + Material Icons from borealis' demo resources,
  a minimal set (not the full wiliwili resource set).
- **`source/`** — app code (Menu/Settings/Player/Language activities),
  mirroring the Android client's structure. `language_activity.{hpp,cpp}`
  is the language picker's own screen — see [Localization](#localization)
  below.

## Localization

`SettingsActivity`'s "Sprache"/"Language" cell pushes `LanguageActivity`, a
plain alphabetically-sorted list (System, plus every language in
[`i18n/strings.json`](../../i18n/strings.json)) — tapping an entry sets
`Prefs.language`, saves it, calls `strings::setLanguage()`, and pops back.
Since borealis keeps a pushed activity's view tree alive rather than
recreating it, `SettingsActivity::onResume()` (called by `popActivity()` on
the activity revealed underneath) reloads `Prefs` from disk and refreshes
its own visible text — nothing updates on its own just because the language
changed one screen up. `prefs.cpp`'s `resolveLanguage()` detects the
console's own system language via `setGetSystemLanguage()`/
`setMakeLanguage()`, falling back to English if that call fails or the
system language isn't one of the five supported.

## Building

Requires devkitPro with `devkitA64`, `libnx`, `switch-glfw`, `switch-mesa`,
`switch-libdrm_nouveau`, `switch-pkg-config`, `switch-zlib`, `switch-cmake`,
`switch-tools`, `dkp-cmake-common-utils`, and `dkp-toolchain-vars` installed
under `$DEVKITPRO`.

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=$DEVKITPRO/devkitA64
export PATH=$DEVKITPRO/tools/bin:$PATH

cmake -S clients/switch -B clients/switch/build -DPLATFORM_SWITCH=ON -DCMAKE_BUILD_TYPE=Release
cmake --build clients/switch/build --target unison-switch.nro
```

Output: `clients/switch/build/unison-switch.nro`. Copy it to the SD card at
`/switch/unison/unison-switch.nro` and launch it through the Homebrew
Menu.

## Status

- [x] Toolchain bootstrap + borealis integration verified (smoke test:
      compiles, links, produces a runnable `.nro`).
- [x] Menu (host entry, UDP discovery-beacon listener, Settings link)
- [x] Settings (on-screen-controls toggle, video filter toggle, key
      bindings per controller button, language picker)
- [x] Player (fullscreen video via NanoVG image, audio via `audout`,
      physical + on-screen touch input)

Everything above compiles and links cleanly to a `.nro` (see build steps),
but hasn't been tested on a real console in this environment for lack of
access to real Switch hardware. Known design decision: since on Switch
(unlike Android, where Back is its own system gesture channel) every
controller button is bound to a GBA button by default, Player is exited by
holding ZL+ZR instead of a Back button.
