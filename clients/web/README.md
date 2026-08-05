# clients/web

Browser client, no install needed: a single `index.html`, served by any
static web server (or opened directly as `file://`, see below). Originally
part of the `dolphin-gba-stream` fork (embedded there in Dolphin's own HTTP
server, usable only against Dolphin) -- split out and generalized here:
connects like the other four clients (Android/Switch/3DS/NDS) to **any**
Unison server (Cemu, Azahar, melonDS, Dolphin), via host/port entry
instead of automatic discovery (browsers can't receive UDP broadcasts, see
"No discovery beacon" below).

## Scope

- Connection setup follows the standard handshake exactly
  (`unison/handshake.h`, `docs/protocol.md`
  "Connection Setup: Handshake"), via a WASM bridge (`wasm_bridge.c`,
  compiled from `core/`) instead of a second, hand-maintained JS
  reimplementation -- exactly the same codec/field semantics as every
  other client. `hello.slots` automatically distinguishes the two cases:
  multiple slots (currently only `GC_GBA_LINK`/Dolphin's lobby) show a
  P1-P4 picker, anything else (a single slot) connects directly, no picker
  detour.
- Video/audio decoding, keyboard rebinding, the touch overlay (mobile
  devices), and optional gamepad binding (Gamepad API) are carried over
  unmodified from the original Dolphin web UI -- see `index.html`'s own
  comments for the details (deflate deliberately stays a native browser
  API, not part of the WASM bridge; see `wasm_bridge.c`'s own comment).
- Its own ping/pong (message types 4/5) for a latency/frame-rate readout
  in the settings menu -- not part of `unison/protocol.h`, a server that
  doesn't know it simply never sends type 5 back, and the readout just
  stays at "--".

## Localization

The settings panel's "Sprache"/"Language" row opens its own overlay
(`#languagePanel`), an alphabetically-sorted list (System, plus every
language in [`i18n/strings.json`](../../i18n/strings.json)) -- picking an
entry applies it immediately and returns to whichever panel (desktop
settings or mobile menu) opened it. `t(key, ...args)` looks up
`STRINGS[currentLang]` (generated into `strings_generated.js`), with
`resolvedLang()` mapping `langPref` ("system" by default, persisted in
`localStorage`) to whichever of the five supported languages
`navigator.language` matches, falling back to English otherwise.

## No discovery beacon

Unlike the four native clients, this one can't receive the UDP beacon
(port 6805, `docs/protocol.md` "Discovery-Beacon (UDP)") -- browsers have
no access to raw UDP sockets. Host and port must therefore be entered by
hand (pre-filled from `?host=&port=` query parameters or the last-used
value). Dolphin's lobby port is always `6800` (the form's default value);
the other emulators each use their own port, configured in their own
settings.

## Building

`index.html` loads `unison_core.js` (the WASM bridge) directly via
`<script src="unison_core.js">` -- both files are committed, a normal
checkout needs no Emscripten. Only rebuild after changing `wasm_bridge.c`
or `core/` itself:

```sh
# once: fetch the emsdk submodule
git submodule add https://github.com/emscripten-core/emsdk.git clients/web/emsdk
git submodule update --init clients/web/emsdk

clients/web/build_wasm.sh
```

Installs the Emscripten toolchain on first run (needs network access for
that, not afterward), builds `core/` for wasm32, links `wasm_bridge.c`
against it into a single `unison_core.js` (`-s SINGLE_FILE=1`, the
`.wasm` binary is embedded inside it), and verifies the result against
`bridge_test.mjs` (real `unison_core` behavior, not just "it compiled").

## Testing/trying it out without a web server

Opening `index.html` directly via `file://` in a browser doesn't work
reliably (some browsers block `fetch`/module behavior on `file://`) --
instead, any plain static server is enough:

```sh
cd clients/web
python3 -m http.server 8000
```

Then open `http://localhost:8000/`.

## Known limitations

- No microphone input (the other clients don't send one back to the
  server either -- microphone forcing is server-side, see the
  Cemu/Azahar/melonDS forks).
- `input_encoding` isn't checked/branched on -- like every other client,
  this one assumes `"gba_buttons"`, the only encoding any server currently
  offers.
