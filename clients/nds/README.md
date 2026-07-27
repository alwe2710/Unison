# clients/nds

Nintendo DS Homebrew-Client (devkitARM / libnds / dswifi). **Machbarkeitstest**,
kein vollwertiger Player: die NDS-WLAN-Hardware ist auf 802.11b (1-2 Mbit/s)
begrenzt, was mit dem aktuellen Wire-Protokoll rein rechnerisch selbst nach der
TILES-Kompressionsänderung eng wird, siehe
[`docs/nds-feasibility.md`](../../docs/nds-feasibility.md). Dieser Client
existiert, um das auf echter Hardware zu verifizieren statt es nur theoretisch
abzuschätzen.

## Umfang

- Verbindet per WLAN (`Wifi_InitDefault(WFC_CONNECT)`, nutzt die auf der Konsole
  hinterlegten WFC-Zugangsdaten aus den Systemeinstellungen), sucht danach
  automatisch im lokalen /24-Subnetz nach einer finlink-Lobby (HTTP GET `/`
  auf Port 6800, wie bei `clients/3ds`/`clients/switch`s Discovery -- siehe
  [`arm9/source/discovery.c`](arm9/source/discovery.c)), und geht dann per
  Slot-Wahl (A/B/X/Y = Slot 1-4) zu einem `StreamHost`-Port. Findet die Suche
  nichts (Server noch nicht gestartet, anderes Subnetz, ...) oder wird sie
  mit START übersprungen, fragt ein On-Screen-Keyboard
  (`nds/arm9/keyboard.h`, wie bei den anderen drei Clients' Software-
  Tastatur für die Host-Eingabe) nach der IP; SELECT im Slot-Menü stößt die
  Suche erneut an, R fragt die IP direkt neu ab. Keine Persistenz über
  Neustarts hinweg -- genau wie bei den anderen drei Clients.
- Dekodiert Video **und** Audio mit demselben `core/` wie alle anderen Clients
  (WS-Handshake/Framing, Deflate, TILES/INDEXED-Formate).
- Zeigt das Video direkt (Hauptbildschirm, `MODE_FB0`, zentriert 240×160 in
  256×192) und läuft laufende Durchsatz-/Framerate-Statistik auf dem
  Unterbildschirm mit (Konsolen-Text).
- **Kein Audio-Playback** (nur mitgezählt, nicht auf einen Soundkanal gelegt) —
  für die Bandbreitenfrage nicht nötig, und der Client könnte den
  Audio-Empfang ohnehin nicht abbestellen (`docs/protocol.md`: kein
  Server-Mechanismus dafür), also ändert Playback am Bandbreitenbedarf nichts.
- **GBA-Tasten werden gesendet** (D-Pad/A/B/L/R/Select/Start, 1:1 wie beim
  echten GBA — `buildGbaKeyMask()`/`sendGbaInput()` in
  [`arm9/source/main.c`](arm9/source/main.c)). **X+Y gemeinsam ~0,6s halten**
  beendet die Verbindung (wie `clients/switch`s ZL+ZR-Hold) — normales
  START/SELECT geht nicht, weil die während einer aktiven Session echte,
  sendbare GBA-Tasten sind und NDS (anders als Switch/3DS) weder einen
  HOME-Knopf noch spontan einen Touch-Bereich für "Trennen" hat, ohne den
  Unterbildschirm mit der Durchsatz-Statistik zu verdecken.

## Server-IP konfigurieren

Kein Compile-Time-`#define` mehr nötig — die automatische Discovery (siehe
"Umfang" oben) findet den Server im selben Subnetz von selbst, und wenn
nicht, fragt ein On-Screen-Keyboard danach (`promptForIp()` in
[`arm9/source/main.c`](arm9/source/main.c)). Muss eine literale
IPv4-Adresse sein, kein Hostname — der Client nutzt bewusst `inet_addr()`
statt `gethostbyname()`, um keinen DNS-Roundtrip über WFC zu brauchen; eine
ungültige Eingabe führt einfach zu einem fehlgeschlagenen Verbindungsversuch,
genau wie bei den anderen drei Clients gibt es keine Formatvalidierung.

## Bauen

Braucht dieselbe devkitARM-Toolchain wie der 3DS-Client, plus `libnds`,
`dswifi`, `maxmod-nds`, `calico` und `ndstool` (alle über das normale
devkitPro-Pacman-Repo).

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM
cd clients/nds
make
```

Erzeugt `finlink-nds.nds`. Zwei getrennt gebaute ELFs (ARM9 + ARM7), per
`ndstool` kombiniert — devkitPro's `NDS.cmake` hat dafür **keinen** Helfer
(anders als bei Switch/3DS/Android), und selbst devkitPro's eigenes
Referenzbeispiel für diesen Fall (`templates/combined`) nutzt klassische
Makefiles statt CMake. Deshalb weicht dieser Client bewusst vom
CMake-Muster der anderen drei Clients ab:

- `arm9/` — der eigentliche finlink-Client (`arm9/source/main.c`), linkt
  `finlink_core` als Quelldateien direkt mit ein (kein
  `add_subdirectory()`-Äquivalent in einem klassischen Makefile).
- `arm7/` — unveränderter "default ARM7 core" aus devkitPro's
  `templates/combined`-Beispiel: NVRAM, erweitertes Keypad, RTC,
  Power-Management, Touch, Sound/Mic, und — der für dieses ARM9-seitige
  `dswifi9` relevante Teil — der Wireless-Manager-Server
  (`wlmgrStartServer()`). Kein finlink-spezifischer Code nötig.

(`calico`'s `ds_rules` hat zwar einen eingebauten Default-ARM7-Mechanismus
für genau diesen Fall — ein vorgefertigtes `calico/bin/ds7_maine.elf`
verlinken statt selbst ein ARM7-ELF zu bauen —, aber das installierte
`calico`-Paket enthielt in dieser Umgebung keine `bin/`-Binaries, nur
`lib/`/`include/`/`share/`. Der Zwei-ELF-Weg über `templates/combined`
umgeht das und ist zusätzlich der von devkitPro selbst dokumentierte Weg.)

## Bekannte Einschränkungen

- Die Subnetz-Suche (`arm9/source/discovery.c`) macht nicht-blockierendes
  `connect()` gefolgt von wiederholten `send()`-Versuchen (EAGAIN/
  EWOULDBLOCK = noch nicht verbunden, echter Fehler = Host tot), weil
  `libnds`/`dswifi` kein `select()`/`poll()` und (getestet auf echter
  Hardware) kein brauchbares `getsockopt(SO_ERROR)` hat -- eine erste
  Version verließ sich darauf, fand aber auf echter Hardware nie einen
  Server, weil jeder Slot einfach bis zum Timeout in "verbindet noch" hing.
- Nur GBA-Auflösung 240×160 unterstützt (hart codiert für statische
  Puffergrößen statt malloc/realloc, siehe Kommentare in `main.c`) — ein
  Frame mit abweichender Auflösung wird als Fehler gezählt, nicht
  angezeigt.
- Keine Fehlerbehandlung für einen vollen Empfangspuffer über einen
  einzelnen (auch unkomprimierten) Videoframe hinaus — bei einem
  fehlerhaften/böswilligen Server wird die Verbindung getrennt statt zu
  überlaufen.
- Menü/Einstellungen/GBA-Tasten-Overlay wie bei den anderen drei Clients
  gibt es hier (noch) nicht — siehe "Umfang" oben.
