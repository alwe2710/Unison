# clients/web

Browser-Client, kein Install nötig: eine einzige `index.html`, per beliebigem
statischen Webserver (oder direkt als `file://`, siehe unten) geöffnet.
Ursprünglich Teil des `dolphin-gba-stream`-Forks (dort in Dolphins eigenen
HTTP-Server eingebettet, nur gegen Dolphin selbst nutzbar) -- hier
herausgelöst und generisch gemacht: verbindet sich wie die anderen vier
Clients (Android/Switch/3DS/NDS) gegen **jeden** finlink-Server (Cemu,
Azahar, melonDS, Dolphin), per Host/Port-Eingabe statt automatischer
Discovery (Browser können kein UDP-Broadcast empfangen, siehe "Kein
Discovery-Beacon" unten).

## Umfang

- Verbindungsaufbau folgt exakt dem Standard-Handshake
  (`finlink/handshake.h`, `docs/protocol.md`
  "Verbindungsaufbau: Handshake"), über eine WASM-Bridge (`wasm_bridge.c`,
  kompiliert aus `core/`) statt einer zweiten, handgepflegten
  JS-Neuimplementierung -- exakt dieselbe Codec-/Feldsemantik wie bei jedem
  anderen Client. `hello.slots` entscheidet automatisch zwischen den zwei
  Fällen: mehrere Slots (aktuell nur `GC_GBA_LINK`/Dolphins Lobby) zeigen
  einen P1-P4-Picker, alles andere (ein Slot) verbindet direkt, ganz ohne
  Picker-Umweg.
- Video-/Audio-Dekodierung, Tastatur-Rebinding, Touch-Overlay (Mobilgeräte)
  und optionale Gamecontroller-Bindung (Gamepad API) sind unverändert aus
  der ursprünglichen Dolphin-Web-UI übernommen -- siehe `index.html`s
  eigene Kommentare für die Details (Deflate bleibt bewusst native
  Browser-API, nicht Teil der WASM-Bridge; siehe `wasm_bridge.c`s
  Kommentar).
- Eigenes Ping/Pong (Nachrichtentyp 4/5) für eine Latenz-/Framerate-Anzeige
  im Einstellungsmenü -- kein Teil von `finlink/protocol.h`, ein Server, der
  es nicht kennt, sendet einfach nie Typ 5 zurück, die Anzeige bleibt dann
  auf "--".

## Kein Discovery-Beacon

Anders als die vier nativen Clients kann dieser hier den UDP-Beacon (Port
6805, `docs/protocol.md` "Discovery-Beacon (UDP)") nicht empfangen -- Browser
haben keinen Zugriff auf rohe UDP-Sockets. Host und Port müssen daher von
Hand eingegeben werden (vorausgefüllt aus `?host=&port=`-Query-Parametern
oder dem zuletzt benutzten Wert). Dolphins Lobby-Port ist immer `6800`
(Standardwert im Formular); die anderen Emulatoren nutzen jeweils einen
eigenen, in ihren Einstellungen konfigurierten Port.

## Bauen

`index.html` lädt `finlink_core.js` (die WASM-Bridge) direkt per
`<script src="finlink_core.js">` -- beide Dateien sind committet, ein
normaler Checkout braucht kein Emscripten. Nur nach einer Änderung an
`wasm_bridge.c` oder `core/` selbst neu bauen:

```sh
# einmalig: emsdk-Submodul holen
git submodule add https://github.com/emscripten-core/emsdk.git clients/web/emsdk
git submodule update --init clients/web/emsdk

clients/web/build_wasm.sh
```

Installiert beim ersten Lauf die Emscripten-Toolchain (braucht dafür
Netzwerkzugriff, danach nicht mehr), baut `core/` für wasm32, linkt
`wasm_bridge.c` dagegen zu einer einzigen `finlink_core.js`
(`-s SINGLE_FILE=1`, das `.wasm`-Binary steckt darin eingebettet) und
verifiziert das Ergebnis gegen `bridge_test.mjs` (echtes
`finlink_core`-Verhalten, nicht nur "hat kompiliert").

## Testen/Ausprobieren ohne Webserver

`index.html` per `file://` direkt im Browser öffnen funktioniert nicht
zuverlässig (manche Browser blockieren `fetch`/Modul-Verhalten auf
`file://`) -- stattdessen reicht ein beliebiger simpler statischer Server:

```sh
cd clients/web
python3 -m http.server 8000
```

Dann `http://localhost:8000/` öffnen.

## Bekannte Einschränkungen

- Kein Mikrofon-Input (die anderen Clients senden auch keins an den Server
  zurück -- Mikrofon-Erzwingung ist serverseitig, siehe die
  Cemu/Azahar/melonDS-Forks).
- `input_encoding` wird nicht geprüft/verzweigt -- wie jeder andere Client
  geht dieser von `"gba_buttons"` aus, dem einzigen Encoding, das aktuell
  irgendein Server anbietet.
