# NDS-Client: Machbarkeitsanalyse (Stand 2026-07-25)

Status: **in Verifikation auf echter Hardware**. Android, 3DS und Switch kamen
zuerst; die Analyse unten war zunächst rein theoretisch (keine NDS-Hardware in
der Entwicklungsumgebung verfügbar). Seit der TILES-Protokolländerung (siehe
[`protocol.md`](./protocol.md)) gibt es außerdem [`clients/nds`](../clients/nds)
als bewusst minimalen Test-Client (kein Menü/Player, nur Verbinden + Dekodieren
+ Durchsatz-/Framerate-Anzeige), um die Zahlen unten gegen einen echten
Server/echte WLAN-Bedingungen zu prüfen, statt weiter nur zu schätzen.

## Kernproblem: WLAN-Hardwarelimit

Die NDS-WLAN-Hardware unterstützt nur die 802.11b-Transferraten **1 und 2 Mbit/s**
(kein 5,5/11 Mbit/s). Reales TCP-Throughput über `dswifi` liegt durch Protokoll-
Overhead noch darunter. Das ist eine harte Hardwaregrenze, keine Softwareschwäche.

## Bandbreiten-Budget gegen das aktuelle Protokoll

- **Audio allein**: z. B. 32 kHz, Stereo, 16-bit PCM → 128.000 B/s ≈ **1,02 Mbit/s**.
  Das entspricht bereits der gesamten realistischen WLAN-Kapazität der NDS —
  noch bevor überhaupt Video dazukommt.
- **Video**: 240×160 × RGB565 = 76.800 B/Frame unkomprimiert, voll (kein TILES).
  Selbst mit optimistischer 3–4×-Deflate-Kompression bleiben ~19–25 kB/Frame —
  bei 60 fps allein schon ~9–12 Mbit/s, weit über dem Hardwarelimit.

### Mit der TILES-Protokolländerung (siehe `protocol.md`)

Seit der `format`-Bitmask (INDEXED/TILES) muss der Server pro Frame nur noch
die 8×8-Kacheln senden, die sich seit dem letzten tatsächlich gesendeten Frame
geändert haben — bei GBA-Spielen mit typischerweise nur teilweise bewegtem
Bildinhalt (HUD/Standbild-Anteile, Textboxen, langsamere Genres) reduziert das
die Video-Bandbreite gegenüber dem Vollbild-Fall deutlich, geschätzt
grob 5–8× bei moderater Bewegung (stark inhaltsabhängig — schnelle
Full-Screen-Scroller profitieren viel weniger als z. B. rundenbasierte
Spiele). Rechnerisch bleibt Video damit potenziell im ein- bis
niedrigen-zweistelligen kB/Frame-Bereich statt ~20-25 kB — aber:

**Audio bleibt von der TILES-Änderung komplett unberührt** (sie betrifft nur
Video-Frames) und ist mit ~1,02 Mbit/s bei 32 kHz/Stereo weiterhin für sich
allein schon nahe der gesamten realistischen WLAN-Kapazität. Selbst ein durch
TILES stark reduzierter Video-Stream plus unverändertes Stereo-Audio bleibt
also rechnerisch eng bis nicht machbar bei voller Framerate — die TILES-
Änderung verschiebt das Verhältnis (Audio wird zum dominanten statt
gleichrangigen Faktor), löst das Grundproblem aber nicht allein.

**Fazit (weiterhin vorwiegend theoretisch, siehe Status oben)**: Voller
Original-Stream (Stereo-Audio + native Framerate) bleibt mit dem aktuellen
Wire-Protokoll auf NDS **rechnerisch eng bis nicht machbar**, auch nach TILES.
Was auf echter Hardware tatsächlich ankommt (u. a. weil reale
`dswifi`/TCP-Overhead und tatsächliche Tile-Änderungsraten schwer präzise
vorherzusagen sind), ist genau die Frage, die [`clients/nds`](../clients/nds)
jetzt empirisch beantworten soll.

## Präzedenzfälle

Bekannte NDS-Homebrew-Streaming-Projekte (z. B. `streamer-ds`) lösen das
vergleichbare Problem nur durch drastisch reduzierte Auflösung/Framerate und
LZ77-Kompression — kein Fall von Vollqualitäts-Streaming über NDS-WLAN gefunden.

## Optionen für einen späteren NDS-Client

1. **Kein/kaum Audio** (z. B. 8 kHz mono ≈ 128 kbit/s) + reduzierte Framerate
   (Schätzung: einstellige fps-Bereich, abhängig von tatsächlicher Kompression).
2. **Serverseitige Protokollerweiterung** um Qualitäts-/Framerate-Verhandlung
   (Änderung am `dolphin-gba-stream`-Fork, außerhalb dieses Repos).
3. NDS **nicht** als Live-Stream-Client, sondern reduzierter Anwendungsfall
   (z. B. nur Status-Anzeige/Lobby via `/status`, kein Video/Audio).

Diese Analyse basiert weiterhin auf dokumentierten Hardware-Grenzwerten, nicht
auf einem Test mit echter Hardware (in dieser Entwicklungsumgebung nach wie vor
nicht verfügbar). Der devkitARM/libnds/dswifi-Toolchain-Teil ist inzwischen
kein Hindernis mehr — [`clients/nds`](../clients/nds) baut und läuft (als
`.nds`-ROM verifiziert) —, aber die eigentliche Zahl (tatsächlicher Durchsatz
über echtes WLAN zu einem echten finlink-Server) kann nur auf echter Hardware
gemessen werden. Vor einer finalen Entscheidung zwischen den Optionen oben
sollte dieses Ergebnis abgewartet werden.

## Quellen

- [DSWifi documentation – BlocksDS](https://blocksds.skylyrac.net/dswifi/)
- [Wi-Fi – BlocksDS Tutorial](https://blocksds.skylyrac.net/tutorial/advanced/wifi/)
- [streamer-ds – GameBrew](https://www.gamebrew.org/wiki/Streamer-ds)
