# clients/3ds

Nintendo 3DS homebrew client (`.3dsx`, devkitARM/libctru + citro2d/citro3d).
Uses the console's two screens directly instead of the
Menu/Settings/Player screen stack the other clients use: **top screen**
always shows the GBA stream (or an idle "finlink" screen before
connecting), **bottom screen** shows Menu/Settings before connecting and
the on-screen touch controls (+ a "Trennen" button) while playing.

## Architektur

Alle Protokoll-/Transport-Logik kommt unverändert aus [`../../core/`](../../core/).
Es gibt kein UI-Framework wie borealis (Switch) hier -- citro2d bietet nur
Grundprimitive (Rechtecke, Text, Bilder), keine fertigen Widgets, daher sind
alle Menüs/Buttons handgerollt (`ui.hpp`), im selben Stil wie das
On-Screen-Touch-Overlay des Switch-Clients (`clients/switch/source/video_view.cpp`).

- **`source/session.{hpp,cpp}`** -- WS-Handshake/Framing + Session-Loop auf
  einem Hintergrund-Thread, praktisch identisch zu
  `clients/switch/source/session.cpp` (portables C++, nur die RNG-Quelle
  unterscheidet sich: `rand()` statt `randomGet()`, da devkitARM dieselbe
  fehlende `getentropy()`-Anbindung hat wie devkitA64).
- **`source/discovery.{hpp,cpp}`** -- UDP-Discovery-Beacon-Listener
  (`BeaconListener`, Port 6805, siehe `docs/protocol.md`
  "Discovery-Beacon (UDP)") auf einem eigenen Hintergrund-Thread, plus
  `/status`-Polling für die Slot-Belegung nach Server-Wahl. `gethostid()`
  liefert nur noch die eigene IP für die Diagnose-Anzeige.
- **`source/video_tex.{hpp,cpp}`** -- GBA-Video als citro3d-Textur.
  RGB565 wird direkt hochgeladen (keine RGBA8-Konvertierung wie beim
  NanoVG-Pfad des Switch-Clients -- GPU_RGB565 entspricht exakt dem
  Wire-Format, was auf der deutlich schwächeren 3DS-CPU zählt). Die Textur
  ist fix 256x256 (PICA200 braucht Zweierpotenzen), gezeichnet wird nur der
  240x160-Teilbereich über eine `Tex3DS_SubTexture`.
- **`source/audio.{hpp,cpp}`** -- Audiowiedergabe über NDSP. Im Gegensatz
  zum Switch-Client (dessen `audout`-Gerät fix auf 48kHz/Stereo steht und
  daher resamplen muss) nimmt NDSP über `ndspChnSetRate()` eine beliebige
  Rate entgegen -- kein Resampling nötig.
- **`source/gba_buttons.hpp`** -- kein Key-Rebinding wie bei Android/Switch:
  das 3DS-Tastenlayout (Steuerkreuz, A/B, L/R, Start/Select) entspricht
  bereits fast 1:1 dem GBA, daher nur eine feste Standardbelegung.
- **`source/ui.hpp`** -- Rechtecke/Buttons/Toggles + manuelles
  Touch-Hit-Testing für Menu/Settings/On-Screen-Controls.

## Building

Benötigt devkitPro mit `devkitARM`, `libctru`, `citro2d`, `citro3d`,
`3dstools`, `general-tools`, `3ds-cmake`, `devkitarm-cmake` und
`3ds-pkg-config` unter `$DEVKITPRO`.

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$DEVKITPRO/portlibs/3ds/bin:$PATH

cmake -S clients/3ds -B clients/3ds/build -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build clients/3ds/build
```

Output: `clients/3ds/build/finlink-3ds.3dsx`. Auf die SD-Karte nach
`/3ds/finlink-3ds.3dsx` kopieren und über den Homebrew Launcher starten.

## Status

- [x] Toolchain-Bootstrap verifiziert (Hello-World-Smoke-Test: kompiliert,
      linkt, erzeugt eine lauffähige `.3dsx`).
- [x] Menu (Host-Eingabe via Software-Keyboard, P1-P4-Picker, LAN-Discovery
      via `gethostid()`, Settings-Link) -- unteres Display
- [x] Settings (On-Screen-Controls-Toggle, Video-Filter-Toggle) -- unteres
      Display
- [x] Player -- Video oberes Display, On-Screen-Controls + physische Tasten
      + "Trennen" unteres Display

Alles oben kompiliert und linkt sauber zu einer `.3dsx`, wurde aber mangels
Zugriff auf echte 3DS-Hardware in dieser Umgebung noch nicht auf einer
Konsole getestet -- insbesondere die Video-Textur-UV-Konvention
(`Tex3DS_SubTexture`) und die NDSP-Audiowiedergabe sind ungetestete
Annahmen aus der citro2d/citro3d-Dokumentation.
