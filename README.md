# finlink

Generisches Client-Framework für den Dolphin GBA-Stream.

## Kontext

Im Fork [dolphin-gba-stream](https://github.com/) (Basis: `dolphin-emu/dolphin`) gibt es ein Feature, das
die integrierte GBA-Emulation (`GBA::Core`, genutzt für GC↔GBA-Link-Cable-Spiele) per eigenem
WebSocket-Protokoll an Browser-Clients streamt, statt Video/Audio/Input lokal zu behalten.

Dieses Repository ist der Ausgangspunkt für ein **generelles Client-Framework** für diesen Stream —
losgelöst von der aktuellen monolithischen, eingebetteten HTML/JS-Client-Implementierung auf
C++-Server-Seite.

### Serverseitige Architektur (C++, bereits vorhanden, nicht Gegenstand dieses Repos)

- **`StreamHost`** (`Source/Core/Core/HW/GBAStreamHost.h/.cpp`): eine Instanz pro GC-Port, der auf
  „GBA (Client-Stream)“ (`CORE_GC_GBA_STREAM`) steht. Läuft auf Port 6801–6804, sendet Video
  (RGB565 + raw-deflate) und Audio (PCM) an genau einen verbundenen Client, empfängt Button-States
  zurück und speist sie via `ControllerEmu::SetInputOverrideFunction` in den GBA-Pad ein.
- **`GBAStreamLobby`** (`GBAStreamLobby.h/.cpp`): referenzgezählter Singleton-Server auf festem
  Port 6800, liefert die Picker-Seite (P1–P4) aus, unabhängig davon welcher GC-Port aktiv ist.
- Gemeinsame Client-Seite liegt aktuell als ein einziger großer eingebetteter HTML/JS-String in
  `GBAStreamClientPage.h` (`kGBAStreamClientHtml`, ein C++ `R"HTML(...)HTML"`-Rohstring), der von
  beiden Servern ausgeliefert wird. Kein Build-Schritt, keine externen JS-Dependencies (bewusst so
  gewählt, um serverseitigen Aufwand minimal zu halten).

### Wire-Protokoll

Siehe [`docs/protocol.md`](docs/protocol.md) — Single Source of Truth, gegen die alle Clients
implementieren.

## Ziel dieses Repos

Ein eigenständiges, generelles Client-Framework für dieses Protokoll bauen — als Ablösung/Ergänzung
zum eingebetteten HTML/JS-String auf Serverseite.

## Architektur

Geteilte Protokoll-/Codec-Logik (WebSocket-Framing, raw-deflate-Inflate, RGB565-Konvertierung,
PCM-Buffering, Input-Encoding) liegt in [`core/`](core/) als portable C-Library. Jede Plattform
bekommt darüber eine dünne Shell für Netzwerk, Rendering, Audio-Ausgabe und Input-Polling, da diese
Teile je nach Zielplattform grundverschieden sind:

| Verzeichnis | Zielplattform | Toolchain | Status |
|---|---|---|---|
| [`clients/android/`](clients/android/) | Android-App | Android SDK/NDK | rohe Demo (verbinden, Video, Audio, Input) |
| [`clients/3ds/`](clients/3ds/) | Nintendo 3DS Homebrew | devkitARM / libctru | funktionsfähig (verbinden, Video, Audio, Input, Discovery) |
| [`clients/switch/`](clients/switch/) | Nintendo Switch Homebrew | devkitA64 / libnx | funktionsfähig (verbinden, Video, Audio, Input, Discovery) |
| [`clients/nds/`](clients/nds/) | Nintendo DS Homebrew | devkitARM / libnds | **zurückgestellt**, siehe [`docs/nds-feasibility.md`](docs/nds-feasibility.md) |

Gemeinsames Logo für alle Clients: [`assets/logo/`](assets/logo/).

Reihenfolge: Android, 3DS, Switch zuerst. Die NDS-WLAN-Hardware ist auf 1–2 Mbit/s begrenzt, was
mit dem aktuellen Protokoll (Stereo-Audio allein braucht bereits ~1 Mbit/s) nicht ausreicht — Details
und Optionen in der Machbarkeitsanalyse.
