# Wire-Protokoll: Dolphin GBA-Stream

Single Source of Truth für das WebSocket-Protokoll, gegen das alle Clients in diesem
Repo implementieren. Server-Referenzimplementierung: `GBAStreamHost` /
`GBAStreamLobby` im `dolphin-gba-stream`-Fork (nicht Teil dieses Repos).

## Endpunkte

| Server | Port | Zweck |
|---|---|---|
| `GBAStreamLobby` | `6800` | Picker-Seite, referenzgezählter Singleton, unabhängig vom aktiven GC-Port |
| `StreamHost` (× GC-Port) | `6801`–`6804` | Ein Slot pro GC-Port, der auf „GBA (Client-Stream)“ steht. Genau ein verbundener Client pro Port. |

## WebSocket, binäre Frames

| Richtung | Typ | Format |
|---|---|---|
| Server → Client | `1` (Video) | `[u8 type=1][u32le width][u32le height][u8 format][raw-deflate-komprimierter Block]` |
| Server → Client | `3` (Audio) | `[u8 type=3][u32le sampleRate][u8 channels][s16le PCM-Samples]` |
| Client → Server | `2` (Input) | `[u8 type=2][u16le keyBitmask]` |

Bitreihenfolge Input-Bitmask (Bit 0 = LSB): `A, B, Select, Start, Right, Left, Up, Down, R, L`

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

## WebSocket-Handshake und -Framing

Serverseitig ist das WebSocket-Handling selbst (nicht nur das App-Layer-Protokoll
oben) handgerollt (`GBAStreamHost::PerformHandshake`, `TryParseWebSocketFrame`,
`SendWebSocketBinaryFrame`), nicht das Standardverhalten einer WS-Library. Für
Clients relevant, insbesondere auf Plattformen ohne eigenen WS-Client
(3DS/Switch-Homebrew):

- Handshake ist Standard-RFC6455: `Sec-WebSocket-Key` → `SHA1(key + "258EAFA5-
  E914-47DA-95CA-C5AB0DC85B11")` → Base64 → `Sec-WebSocket-Accept`, vom Client
  zu verifizieren.
- Server-Frames sind immer unmaskiert, `FIN=1`, Opcode `0x2` (Binary), 7/16/64-Bit
  Längenfeld je nach Payload-Größe.
- Client-Frames müssen laut RFC **maskiert** gesendet werden.
- **Keine Fragmentierung** (`FIN=0` gilt als Protokollfehler, wird vom Server
  weder gesendet noch akzeptiert), **kein Ping/Pong**, **kein
  `permessage-deflate`** — die Deflate-Kompression passiert ausschließlich
  manuell auf dem Video-Payload (siehe oben), nicht auf WS-Ebene.
- Server schickt beim Schließen keinen Close-Frame zurück; nach Senden/Empfangen
  eines Close-Frames (`Opcode 0x8`) einfach die TCP-Verbindung schließen.

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

Response hat CORS-Header gesetzt (dient der Lobby-Belegungsanzeige). Die Lobby
(Port 6800) liefert auf jedem Pfad unbedingt dieselbe HTML-Seite aus — es gibt
dort **keinen** gebündelten Status über alle vier Player-Ports. Ein eigener
Picker (statt der eingebetteten HTML-Lobby) muss `/status` selbst einzeln auf
6801–6804 pollen.

## Bekannte Einschränkungen / offene Fragen

- Sample-Rate und Kanalzahl des Audio-Streams sind serverseitig konfigurierbar
  (im Frame-Header übertragen), nicht fix — Clients müssen sie pro Stream auslesen,
  nicht hart annehmen.
- Video-Auflösung entspricht dem GBA-Screen (240×160), wird aber ebenfalls im
  Frame-Header übertragen statt hart angenommen.
- Kein eingebauter Mechanismus für Qualitäts-/Framerate-Verhandlung durch den
  Client — der Server sendet, was der emulierte Kern produziert. Relevant für
  bandbreitenschwache Targets, siehe [`nds-feasibility.md`](./nds-feasibility.md).
