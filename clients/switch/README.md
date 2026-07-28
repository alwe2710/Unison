# clients/switch

Nintendo Switch Homebrew client (`.nro`, devkitA64/libnx). Same structure as
[`../android/`](../android/): **Menu** (host entry + network discovery + link
to Settings), **Settings** (on-screen-controls toggle, video filter toggle,
physical key bindings), **Player** (fullscreen video/audio/input). Native
"Horizon" UI look via [borealis](https://github.com/xfangfang/borealis).

## Architektur

Alle Protokoll-/Transport-Logik kommt unverändert aus [`../../core/`](../../core/)
(via `add_subdirectory`, siehe `CMakeLists.txt`) — dieser Client fügt nur die
Switch-spezifische UI (borealis) und Session-Orchestrierung hinzu.

- **`borealis/`** — vendored `library/`-Unterverzeichnis von
  [xfangfang/borealis](https://github.com/xfangfang/borealis) (Apache 2.0),
  als plain source statt Submodule, da nur der Switch+glfw-Treiber benötigt
  wird. Boreals eigene `glfw`/`SDL`-Submodule sind nur für dessen
  `PLATFORM_DESKTOP`-Build nötig und werden hier nicht eingebunden.
- **`resources/`** — Font + Material-Icons aus borealis' Demo-Ressourcen,
  minimal (nicht der volle wiliwili-Ressourcensatz).
- **`source/`** — App-Code (Menu/Settings/Player-Activities), analog zum
  Android-Client.

## Building

Benötigt devkitPro mit `devkitA64`, `libnx`, `switch-glfw`, `switch-mesa`,
`switch-libdrm_nouveau`, `switch-pkg-config`, `switch-zlib`, `switch-cmake`,
`switch-tools`, `dkp-cmake-common-utils` und `dkp-toolchain-vars` unter
`$DEVKITPRO` installiert.

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=$DEVKITPRO/devkitA64
export PATH=$DEVKITPRO/tools/bin:$PATH

cmake -S clients/switch -B clients/switch/build -DPLATFORM_SWITCH=ON -DCMAKE_BUILD_TYPE=Release
cmake --build clients/switch/build --target finlink-switch.nro
```

Output: `clients/switch/build/finlink-switch.nro`. Auf die SD-Karte nach
`/switch/finlink/finlink-switch.nro` kopieren und über das Homebrew Menu
starten.

## Status

- [x] Toolchain-Bootstrap + borealis-Integration verifiziert (Smoke-Test:
      kompiliert, linkt, erzeugt eine lauffähige `.nro`).
- [x] Menu (Host-Eingabe, UDP-Discovery-Beacon-Listener, Settings-Link)
- [x] Settings (On-Screen-Controls-Toggle, Video-Filter-Toggle, Key-Bindings
      per Controller-Taste)
- [x] Player (Fullscreen Video via NanoVG-Image, Audio via `audout`,
      physische + On-Screen-Touch-Eingabe)

Alles oben kompiliert und linkt sauber zu einer `.nro` (siehe Build-Schritte),
wurde aber mangels Zugriff auf echte Switch-Hardware in dieser Umgebung noch
nicht auf einer Konsole getestet. Bekannte Design-Entscheidung: da auf dem
Switch (anders als Android, wo Zurück ein eigener System-Gesture-Kanal ist)
alle Controller-Tasten per Default an GBA-Buttons gebunden sind, verlässt man
den Player durch Halten von ZL+ZR statt eines Zurück-Buttons.
