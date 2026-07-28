# Wire-Protokoll: Dolphin GBA-Stream

Single Source of Truth für das WebSocket-Protokoll, gegen das alle Clients in diesem
Repo implementieren. Server-Referenzimplementierung: `GBAStreamHost` /
`GBAStreamLobby` im `dolphin-gba-stream`-Fork (nicht Teil dieses Repos). Ab
`protocol_version = 2` (siehe unten) beschreibt dieses Dokument zusätzlich
Discovery-Beacon, Verbindungs-Handshake (inkl. Slot-Aushandlung) und
Downscaling-Verhandlung — vorher gab es dafür keinen Mechanismus, siehe Git-Historie
dieser Datei für den reinen `Video`/`Audio`/`Input`-Stand ohne Handshake.

## Endpunkte

| Server | Port | Zweck |
|---|---|---|
| `GBAStreamLobby` | `6800` (TCP) | Handshake-Einstiegspunkt (siehe unten). Referenzgezählter Singleton, unabhängig vom aktiven GC-Port. Ersetzt die frühere HTML-Picker-Seite. |
| `StreamHost` (× GC-Port) | `6801`–`6804` (TCP) | Ein Slot pro GC-Port, der auf „GBA (Client-Stream)“ steht. Genau ein verbundener Client pro Port. Bei Stream-Typen mit nur einem Slot (siehe unten) wird dieser Bereich nicht benutzt — die Session bleibt auf `6800`. |
| Discovery-Beacon | `6805` (UDP, Broadcast) | Periodische Server-Ankündigung, siehe [Discovery-Beacon](#discovery-beacon-udp). |

> `GBAStreamLobby` unterscheidet seit `protocol_version = 2` zwischen einem
> plain `GET /` (liefert eine HTML-Seite, Status 200 — der WASM-basierte, auf
> `core/` umgestellte Web-Client) und einem WebSocket-Upgrade-Request (löst
> den Handshake unten aus). Alle fünf Clients (Android, 3DS, Switch, NDS/DSi,
> Web) sprechen inzwischen den vollen App-Handshake auf den Player-Ports
> (6801–6804) und nutzen den [UDP-Discovery-Beacon](#discovery-beacon-udp)
> zum Finden eines Servers — die früheren `probeLobby()`/Subnetz-Sweep-
> Implementierungen auf 3DS/Switch/NDS sind vollständig ersetzt.

## Protokollversion

`protocol_version` ist ein einfacher, monoton steigender Integer. Aktueller Wert:
**2**. Kompatibilitätsregel: **exakte Übereinstimmung** — ein Client mit
`protocol_version = 2` verbindet sich nur mit einem Server, der ebenfalls exakt `2`
meldet, und umgekehrt. Kein Major/Minor-Schema; jede Protokolländerung, die dieses
Dokument betrifft, erhöht den Wert um 1.

`protocol_version` taucht an zwei Stellen auf:

1. Im [Discovery-Beacon](#discovery-beacon-udp) — erlaubt es dem Client,
   inkompatible Server bereits in der Serverliste zu markieren, bevor überhaupt
   eine Verbindung versucht wird.
2. In der `hello`-Nachricht des [Handshakes](#verbindungsaufbau-handshake) — letzte,
   verbindliche Prüfung, falls ein Client ohne (oder mit ignoriertem) Beacon direkt
   verbindet.

Ein Client **muss** bei Versionsinkompatibilität den Verbindungsversuch abbrechen
und eine für den Nutzer verständliche Fehlermeldung anzeigen, z. B.:

> Server spricht Protokollversion 3, dieser Client unterstützt nur Version 2 —
> bitte Client oder Server aktualisieren.

Silent-Ignore, Absturz oder ein unklar hängender Zustand sind explizit falsches
Verhalten. Das gilt auch für den Fall, dass gar keine `hello`-Nachricht innerhalb
von 3000 ms nach WebSocket-Upgrade eintrifft (typischerweise ein Server, der den
Handshake überhaupt nicht kennt, faktisch `protocol_version = 1`) — Client-seitig
identisch als Versions-/Kompatibilitätsfehler behandeln, nur mit angepasstem Text
(„Server unterstützt dieses Protokoll nicht“ statt einer konkreten Versionszahl).

## Discovery-Beacon (UDP)

Jeder Server sendet alle **2000 ms** ein UDP-Broadcast-Paket auf Port `6805`
(Subnet-Broadcast bzw. `255.255.255.255`), JSON-kodiert, UTF-8, ein Paket pro
Broadcast (kein Fragmentieren):

```json
{
  "type": "finlink_beacon",
  "protocol_version": 2,
  "emulator_identifier": "Dolphin",
  "game_title": "Pokémon Mystery Dungeon: Team Rot",
  "stream_type": "GC_GBA_LINK",
  "host": "192.168.1.42",
  "handshake_port": 6800
}
```

- `type`: fester String-Marker `"finlink_beacon"`, erlaubt günstiges Verwerfen von
  UDP-Rauschen anderer Anwendungen auf demselben Port, bevor der Rest geparst wird.
- `protocol_version`: siehe oben.
- `emulator_identifier`: freier String, z. B. `"Dolphin"`, `"Azahar"` — nur zur
  Anzeige, kein Enum (neue Emulatoren brauchen keine Protokolländerung).
- `game_title`: aktuell laufender Spieltitel. **Nur für die Server-Suche/Liste
  relevant**, bewusst kein Teil des Handshakes — ein Spielwechsel innerhalb einer
  laufenden Session ist nicht vorgesehen, es gibt daher keinen Bedarf, dieses Feld
  nach Verbindungsaufbau erneut zu übertragen oder zu aktualisieren.
- `stream_type`: siehe [Stream-Typen](#stream-typen). Ein Server kündigt genau den
  einen Typ an, den er in diesem Moment anbietet.
- `host` / `handshake_port`: Zieladresse für den vollen Handshake nach Auswahl
  durch den Nutzer. `handshake_port` ist bei allen aktuell bekannten Stream-Typen
  `6800`, wird aber explizit übertragen statt hart angenommen, für spätere
  Flexibilität.

Client-seitiges Verhalten: eingehende Beacons nach `(host, handshake_port)` in einer
Liste sammeln/aktualisieren; ein Eintrag ohne neuen Beacon für **>6000 ms** (drei
verpasste Intervalle) gilt als verschwunden und wird aus der Liste entfernt. Ein
Eintrag mit abweichender `protocol_version` wird weiterhin angezeigt (Transparenz:
der Server ist ja da), aber als inkompatibel markiert und nicht auswählbar — siehe
[Protokollversion](#protokollversion).

Diese Logik gehört plattformunabhängig in `core/` (nicht in eine einzelne
Client-Shell), analog zur bestehenden Websocket-/Protokoll-Logik dort.

Bekannt und bewusst zurückgestellt: der Beacon ist unauthentifiziert, ein anderes
Gerät im selben LAN könnte theoretisch falsche Werte senden (Spoofing). Für den
privaten Einsatz im eigenen Netz vorerst kein Muss, siehe Backlog im Projektauftrag.

## Stream-Typen

Erweiterbares Enum, als String übertragen (nicht als Zahl/Bitmaske) — ein
unbekannter String lässt sich clientseitig eindeutig als „kenne ich nicht“
behandeln, ohne mit einer ungültigen Zahlen-Kombination verwechselt zu werden:

| `stream_type` | Bedeutung | Slots | Audio |
|---|---|---|---|
| `GC_GBA_LINK` | Dolphins integrierte GBA-Emulation (GC↔GBA-Link-Cable) | 4 (P1–P4) | ja |
| `N3DS_BOTTOM_SCREEN` | Azahar, Bottom Screen (320×240) | 1 | nein |
| `NDS_BOTTOM_SCREEN` | melonDS, Bottom Screen (256×192) | 1 | nein |
| `WIIU_GAMEPAD` | Cemu, GamePad-Bildschirm (854×480) | 1 | nein |

Stream-Typen ohne Audio (`N3DS_BOTTOM_SCREEN`, `NDS_BOTTOM_SCREEN`, `WIIU_GAMEPAD`)
lassen das `audio`-Feld in `hello` weg (`null`/nicht vorhanden) und die
Audio-Verhandlung in `hello_ack` entfällt vollständig — es gibt in diesem Fall zu
keinem Zeitpunkt eine `type=3`-Audio-Message auf der Verbindung.

### Zielbildschirm auf Zweitbildschirm-Clients (3DS, DS/DSi)

`N3DS_BOTTOM_SCREEN`, `NDS_BOTTOM_SCREEN` und `WIIU_GAMEPAD` sind selbst schon der
*Zweitbildschirm* einer entfernten Dual-Screen-Quelle — auf einem Client mit zwei
eigenen Bildschirmen (3DS, DS/DSi) landet ihr Bild deshalb immer zwingend auf dessen
eigenem unteren/zweiten Bildschirm, unabhängig von einer sonst wählbaren
Bildschirm-Einstellung (die nur für Einzelbildschirm-Typen wie `GC_GBA_LINK`
greift). `core/`s `finlink_stream_type_prefers_secondary_screen()`
(`finlink/handshake.h`) kapselt genau diese Zuordnung, damit sie nicht in jedem
Client einzeln dupliziert wird.

## Verbindungsaufbau: Handshake

Vor dem ersten `Video`/`Audio`/`Input`-Binärframe (siehe unten) tauschen Server und
Client vier mögliche JSON-Textnachrichten (WebSocket-Opcode `0x1`, nicht `0x2`) aus.
Framing-Regeln (unmaskierte Server-Frames, maskierte Client-Frames, keine
Fragmentierung, siehe [WebSocket-Transport](#websocket-transport-rfc6455-und-binär-framing))
gelten identisch für Text- wie Binärframes. Jede Nachricht ist ein einzelnes
JSON-Objekt mit Pflichtfeld `"message"` als Diskriminator.

### Ablauf

```
Client                                    Server (Port 6800, GC_GBA_LINK-Beispiel)
  |--- WebSocket-Upgrade (RFC6455) ------->|
  |<-- hello -------------------------------|
  |--- hello_ack --------------------------->|
  |<-- session_ready { redirect: 6801 } ----|      (nur bei Stream-Typen mit >1 Slot)
  (WS-Verbindung schließt; neue Verbindung zu Port 6801)
  |--- WebSocket-Upgrade (RFC6455) ------->|
  |<-- hello -------------------------------|
  |--- hello_ack --------------------------->|
  |<-- session_ready (ohne redirect) -------|
  |<== ab hier: Video (1) / Audio (3), Input (2) wie gewohnt ==>|
```

Bei Stream-Typen mit genau einem Slot (`N3DS_BOTTOM_SCREEN`, künftig
`NDS_BOTTOM_SCREEN`) entfällt der Redirect-Schritt: `session_ready` kommt bereits
auf der Port-6800-Verbindung ohne `redirect`-Feld, und genau diese Verbindung trägt
danach auch den Stream — es gibt für diese Typen keine Verwendung von 6801–6804.

### `hello` (Server → Client)

Erste Nachricht, direkt nach dem WebSocket-Upgrade, unaufgefordert vom Server
gesendet:

```json
{
  "message": "hello",
  "protocol_version": 2,
  "stream_type": "GC_GBA_LINK",
  "slots": [
    { "index": 0, "label": "P1", "occupied": false },
    { "index": 1, "label": "P2", "occupied": true },
    { "index": 2, "label": "P3", "occupied": false },
    { "index": 3, "label": "P4", "occupied": false }
  ],
  "video": { "width": 240, "height": 160, "pixel_format": "rgb565", "fps": 59.7275 },
  "audio": { "sample_rate": 32768, "channels": 2 },
  "input_encoding": "gba_buttons"
}
```

- `slots`: bei `GC_GBA_LINK` die vier GC-Ports. Bei Ein-Slot-Typen ein Array mit
  genau einem Eintrag (`index: 0`) — dieselbe Nachrichtenform bleibt so über alle
  Stream-Typen einheitlich, auch wenn die Auswahl dort trivial ist.
- `video` / `audio`: **native** Parameter, wie sie der Emulator-Kern tatsächlich
  produziert, unabhängig davon, was der Client nachher anfordert. `audio` fehlt
  (oder ist `null`) bei Stream-Typen ohne Audioübertragung.
- `input_encoding`: Name des Input-Encodings, das dieser Stream-Typ auf dieser
  Verbindung erwartet (`type=2`-Messages, siehe unten). `"gba_buttons"` ist das
  bestehende `u16le`-Bitmask-Format, unverändert; `"n3ds_touch"` (für
  `N3DS_BOTTOM_SCREEN`) ist Touch-Position + Press-Status — siehe
  [WebSocket, binäre Frames](#websocket-binäre-frames).

### `hello_ack` (Client → Server)

Antwort des Clients:

```json
{
  "message": "hello_ack",
  "protocol_version": 2,
  "requested_slot": 0,
  "video_limits": { "max_width": 240, "max_height": 160, "max_fps": 60, "max_bitrate_kbps": null },
  "audio_limits": { "max_sample_rate": 32768, "max_channels": 2 }
}
```

- `protocol_version`: die eigene, vom Client unterstützte Version — erlaubt dem
  Server eine defensive Zweitprüfung (siehe [Protokollversion](#protokollversion));
  primär prüft aber bereits der Client die `hello.protocol_version`, bevor er
  überhaupt antwortet.
- `requested_slot`: Index aus der `slots`-Liste des `hello`. Bei Ein-Slot-Typen
  immer `0`.
- `video_limits`: Obergrenzen, die der Client verkraftet. `max_bitrate_kbps` ist
  optional (`null` = kein Limit bekannt/gewünscht) und dient dem Server nur als
  grober Hinweis, wie aggressiv herunterskaliert werden sollte.
- `audio_limits`: fehlt (oder `null`), wenn der Client keinen Ton möchte/kann,
  **oder** wenn `hello.audio` bereits fehlte (Stream-Typ ohne Audio) — in dem Fall
  gibt es hier nichts zu verhandeln.

### `session_ready` (Server → Client)

Bestätigung nach Abgleich von nativen Parametern gegen die Client-Limits — native
Werte haben Priorität, sofern der Client sie laut `hello_ack` verkraftet, sonst
skaliert der Server herunter:

```json
{
  "message": "session_ready",
  "slot": 0,
  "video": { "width": 240, "height": 160, "fps": 59.7275 },
  "audio": { "sample_rate": 32768, "channels": 2 }
}
```

Optional zusätzlich `"redirect": { "host": "192.168.1.42", "port": 6801 }` — nur
bei Stream-Typen mit mehr als einem Slot. Ist `redirect` gesetzt, trägt **diese**
Verbindung keinerlei Video-/Audio-/Input-Frames; der Server schließt sie nach dem
Senden. Der Client öffnet eine neue WebSocket-Verbindung zu `redirect.host:port`
und durchläuft dort denselben `hello`/`hello_ack`/`session_ready`-Austausch erneut
(mit denselben Limits/demselben `requested_slot`) — diesmal ohne `redirect` in der
Antwort. Der zweite Durchlauf ist bewusst eine vollständige Wiederholung statt
eines Tokens/einer Session-Übergabe: hält `GBAStreamHost` unabhängig von
`GBAStreamLobby` (kein geteilter Reservierungszustand zwischen beiden Objekten
nötig) und macht jede der beiden Verbindungen für sich genommen vollständig
selbsterklärend.

`audio` fehlt in `session_ready`, wenn es schon in `hello` fehlte.

Nach einem `session_ready` ohne `redirect` beginnt der Server mit
`Video`/`Audio`-Binärframes im (ggf. herunterskalierten) `width`/`height`/
`sample_rate`/`channels`. Das bestehende Binärformat selbst (Frame-Header trägt
`width`/`height` bereits pro Frame, siehe unten) ändert sich für Downscaling
**nicht** — Downscaling ist rein eine serverseitige Entscheidung, in welcher
Auflösung/Framerate/Samplerate encodiert wird, bevor die bestehende
Header+Deflate-Pipeline greift.

### `handshake_error` (Server → Client)

Ersetzt `session_ready`, kann an dessen Stelle zu jedem Zeitpunkt nach `hello_ack`
kommen (oder statt eines `hello`, falls der Server selbst schon vorab weiß, dass
er nicht bedienen kann — z. B. Versions-Fehlschlag ohne Wartezeit):

```json
{
  "message": "handshake_error",
  "code": "slot_unavailable",
  "detail": "Slot P2 wurde inzwischen von einem anderen Client belegt."
}
```

`code` ∈ `version_mismatch`, `slot_unavailable`, `malformed_request` (erweiterbar).
`detail` ist ein für Menschen lesbarer Text, den der Client direkt anzeigen darf
(muss nicht selbst pro `code` übersetzen, sollte `code` aber zusätzlich fürs
programmatische Verhalten auswerten, z. B. um bei `slot_unavailable` automatisch
die aktualisierte `slots`-Liste erneut anzufragen statt komplett abzubrechen).

Der Server schließt die Verbindung direkt nach dem Senden von
`handshake_error` (kein separates Close-Frame, siehe
[WebSocket-Transport](#websocket-transport-rfc6455-und-binär-framing)) — es gibt
keinen Mechanismus, auf derselben Verbindung mit einem neuen `hello_ack` erneut
zu antworten. „Automatisch erneut anfragen" bedeutet also: eine neue
WebSocket-Verbindung zum selben Handshake-Endpunkt öffnen und dort einen neuen
`hello`/`hello_ack`-Austausch beginnen, nicht denselben Socket weiterbenutzen.

`slot_unavailable` ist ein normaler, erwartbarer Fall (Race zwischen zwei
Clients, die denselben freien Slot zwischen `hello` und `hello_ack` wählen) — kein
Bug, keine Ausnahmesituation, die Client-UI sollte das entsprechend undramatisch
behandeln (z. B. „Slot P2 ist inzwischen belegt, bitte anderen wählen“ statt einer
generischen Fehlermeldung).

## WebSocket, binäre Frames

| Richtung | Typ | Format |
|---|---|---|
| Server → Client | `1` (Video) | `[u8 type=1][u32le width][u32le height][u8 format][raw-deflate-komprimierter Block]` |
| Server → Client | `3` (Audio) | `[u8 type=3][u32le sampleRate][u8 channels][s16le PCM-Samples]` |
| Client → Server | `2` (Input, `input_encoding = "gba_buttons"`) | `[u8 type=2][u16le keyBitmask]` |
| Client → Server | `2` (Input, `input_encoding = "n3ds_touch"`) | `[u8 type=2][u8 pressed][u16le x][u16le y]` |

Diese Binärframes (Opcode `0x2`) treten ausschließlich **nach** einem erfolgreichen
Handshake (`session_ready` ohne `redirect`, siehe oben) auf derselben Verbindung
auf. Inhaltlich unverändert gegenüber der Vor-Handshake-Version des Protokolls;
`width`/`height`/`sampleRate`/`channels` in den Headern spiegeln die in
`session_ready` bestätigten (ggf. herunterskalierten) Werte.

Beide `type=2`-Formen teilen sich denselben Nachrichtentyp — welche Form auf
einer Verbindung gilt, legt `hello.input_encoding` einmalig beim Handshake fest
(siehe oben), nicht ein zusätzliches Unterscheidungsbyte im Frame selbst.

Bitreihenfolge Input-Bitmask (Bit 0 = LSB, `"gba_buttons"`): `A, B, Select, Start, Right, Left, Up, Down, R, L`

`"n3ds_touch"` (für alle Sekundärbildschirm-Stream-Typen mit Touch-Eingabe --
aktuell `N3DS_BOTTOM_SCREEN`, `NDS_BOTTOM_SCREEN` und `WIIU_GAMEPAD`, trotz
des Namens nicht 3DS-spezifisch): `x`/`y` sind Pixel-Koordinaten im nativen
Raster des jeweiligen Stream-Typs, wie in `hello.video`/`session_ready.video`
als `width`/`height` deklariert (`320x240` bei `N3DS_BOTTOM_SCREEN`, `256x192`
bei `NDS_BOTTOM_SCREEN`, `854x480` bei `WIIU_GAMEPAD`) — wie ein Client von
seiner eigenen Eingabe (Touch, Maus, Stick, ...) auf diesen Bereich abbildet,
ist allein seine Sache. Der Encoding-Name selbst bleibt `"n3ds_touch"` über
alle diese Stream-Typen hinweg (keine Wire-Format-Änderung, nur diese
Doku-Präzisierung -- ein neuer Name pro Stream-Typ hätte hier nur 100%
identischen Code unter mehreren Namen dupliziert).
`pressed = 0` bedeutet **loslassen**; `x`/`y` sind dabei bedeutungslos und
müssen `0` sein — ein Loslassen hat keine sinnvolle Position, es ist schlicht
„nicht mehr berühren", nicht „Berührung endete bei (x,y)". Ein Drag wird als
Folge von `pressed = 1`-Frames mit aktualisierten `x`/`y` übertragen, kein
eigener Nachrichtentyp dafür nötig.

Alle Mehrbyte-Felder sind Little-Endian.

### Video-Frame-Payload (`format`-abhängig)

Das `format`-Byte liegt **vor** dem komprimierten Block (unkomprimiert) und
ist eine Bitmaske aus zwei unabhängigen Flags, die zusammen den Inhalt des
entpackten Blocks bestimmen. Der Server wählt pro Frame automatisch die
günstigste Kombination — Clients müssen alle vier unterstützen, nicht nur
`format = 0`.

- Bit 0 (`0x01`, INDEXED): Pixel sind Palette-Indizes (1 Byte) statt
  Rohfarbe (`u16le` RGB565).
- Bit 1 (`0x02`, TILES): nur die 8×8-Kacheln, die sich seit dem letzten
  tatsächlich gesendeten Frame geändert haben, sind enthalten, plus eine
  Liste, welche das sind. Alle anderen Pixel behalten ihren Wert vom
  letzten Frame — der Client-Framebuffer muss also zwischen Frames
  bestehen bleiben, nicht pro Frame neu angelegt werden. Fehlt das Bit,
  ist es das komplette Bild (überschreibt den ganzen Framebuffer).

Entpackter Block, in dieser Reihenfolge (jeder Abschnitt nur vorhanden,
wenn das jeweilige Bit gesetzt ist):

```
[ falls TILES:   u16le tile_count
                 tile_count × u16le tile_index ]
[ falls INDEXED: u16le palette_count            (1-256)
                 palette_count × u16le RGB565 ]
pixel_data:      falls TILES:   tile_count × 64 Pixel (8×8, row-major pro
                                 Kachel, in Reihenfolge der tile_index-Liste)
                 sonst:         width × height Pixel (row-major, ganzes Bild)
                 Pixel:         1 Byte Palette-Index (INDEXED) oder u16le
                                 RGB565 (sonst)
```

`tile_index` ↔ Position: `tiles_per_row = ceil(width/8)`; `tile_col =
tile_index % tiles_per_row`, `tile_row = tile_index / tiles_per_row`
(Integer-Division); Pixel-Ursprung der Kachel = `(tile_col*8, tile_row*8)`.

Das allererste Frame nach Verbindungsaufbau hat serverseitig immer
TILES unset (volles Bild, kein vorheriger Frame zum Diffen) — dient
gleichzeitig als Keyframe. Clients müssen ihren Framebuffer beim
(Wieder-)Verbindungsaufbau selbst zurücksetzen, damit vor diesem ersten
Frame keine Reste einer alten Session sichtbar sind.

Pixelfarbe: bei INDEXED `color = palette[index]`, danach wie gehabt
`r=(color>>11)&0x1F, g=(color>>5)&0x3F, b=color&0x1F`.

Referenzimplementierung: [`../core/include/finlink/protocol.h`](../core/include/finlink/protocol.h)
(`finlink_video_format`, `finlink_decode_video_frame`,
`finlink_video_max_inflated_size`).

## WebSocket-Transport (RFC6455) und Binär-Framing

Serverseitig ist das WebSocket-Handling selbst (nicht nur das App-Layer-Protokoll
oben) handgerollt (`GBAStreamHost::PerformHandshake`, `TryParseWebSocketFrame`,
`SendWebSocketBinaryFrame`), nicht das Standardverhalten einer WS-Library. Für
Clients relevant, insbesondere auf Plattformen ohne eigenen WS-Client
(3DS/Switch-Homebrew):

- Handshake ist Standard-RFC6455: `Sec-WebSocket-Key` → `SHA1(key + "258EAFA5-
  E914-47DA-95CA-C5AB0DC85B11")` → Base64 → `Sec-WebSocket-Accept`, vom Client
  zu verifizieren. (Nicht zu verwechseln mit dem App-Layer-„Handshake“
  aus [Verbindungsaufbau](#verbindungsaufbau-handshake) oben, der darauf aufbaut.)
- Server-Frames sind immer unmaskiert, `FIN=1`, Opcode `0x2` (Binary) für
  Video/Audio oder `0x1` (Text) für die Handshake-JSON-Nachrichten, 7/16/64-Bit
  Längenfeld je nach Payload-Größe.
- Client-Frames müssen laut RFC **maskiert** gesendet werden, unabhängig vom
  Opcode.
- **Keine Fragmentierung** (`FIN=0` gilt als Protokollfehler, wird vom Server
  weder gesendet noch akzeptiert), **kein Ping/Pong**, **kein
  `permessage-deflate`** — die Deflate-Kompression passiert ausschließlich
  manuell auf dem Video-Payload (siehe oben), nicht auf WS-Ebene.
- Server schickt beim Schließen keinen Close-Frame zurück; nach Senden/Empfangen
  eines Close-Frames (`Opcode 0x8`) einfach die TCP-Verbindung schließen. Gilt
  auch für den serverseitigen Verbindungsabbau nach einem `redirect` in
  `session_ready`.

Client-seitige Implementierung dieses Teils liegt in
[`../core/include/finlink/websocket.h`](../core/include/finlink/websocket.h).

## Frame-Semantik (Video-Dedup)

Der Server überspringt Video-Frames, die pixelgleich zum zuletzt gesendeten
Frame sind. Ausbleiben einer neuen Video-Message ist daher normal, kein
Timeout-/Fehlerzustand — Clients müssen einfach das zuletzt empfangene Bild
weiter anzeigen.

## HTTP

`GET /status` — nur auf Player-Ports (6801–6804), nicht auf der Lobby.

```json
{ "occupied": true }
```

Response hat CORS-Header gesetzt. Seit Einführung des Handshakes (`slots` in
`hello`, siehe oben) ist dies **nicht mehr der primäre Weg**, um Slot-Belegung zu
erfahren — der Handshake liefert dieselbe Information atomar als Teil der
Verbindungsaufnahme und vermeidet damit das Race zwischen „Status pollen“ und
„danach separat verbinden“. `/status` bleibt als sekundärer/diagnostischer
Endpunkt bestehen, ist aber für neue Client-Implementierungen nicht mehr nötig.
Die Lobby (Port 6800) hat kein HTTP-Äquivalent von `/status` über alle vier
Player-Ports hinweg — die `slots`-Liste im Handshake ersetzt das. Ein plain
`GET /` dort liefert weiterhin HTML aus (siehe Hinweis unter
[Endpunkte](#endpunkte)), aber das ist der Web-Client, kein Discovery-
Mechanismus für die nativen Clients — die nutzen den UDP-Beacon.

## Bekannte Einschränkungen / offene Fragen

- `NDS_BOTTOM_SCREEN` ist als `stream_type`-Wert reserviert, aber nirgends
  implementiert. Kein Server sendet diesen Wert aktuell.
- Der Discovery-Beacon ist unauthentifiziert (siehe
  [Discovery-Beacon](#discovery-beacon-udp)) — Härtung dagegen ist bewusst
  zurückgestellt.
- Zwischen `hello` und `hello_ack` gibt es keine serverseitige Reservierung eines
  Slots — zwei Clients können denselben freien Slot gleichzeitig anfordern; der
  Verlierer bekommt `handshake_error` mit `code = "slot_unavailable"` und muss
  selbst erneut wählen (siehe oben). Das ist erwartetes Verhalten, kein Bug.
- Ob RGB565+raw-deflate für `N3DS_BOTTOM_SCREEN` (320×240, größer als GBA
  240×160) unverändert taugt oder ein anderer Codec nötig ist, ist noch offen —
  wird im Zuge der Azahar-Implementierung geklärt, nicht Teil dieser
  Protokollrevision.
