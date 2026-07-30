# finlink

Generic client framework for the Dolphin GBA stream.

## Context

The [dolphin-gba-stream](https://github.com/) fork (based on `dolphin-emu/dolphin`) has a feature
that streams the built-in GBA emulation (`GBA::Core`, used for GC↔GBA Link Cable games) to browser
clients over its own WebSocket protocol, instead of keeping video/audio/input local.

This repository is the starting point for a **general client framework** for that stream —
decoupled from the current monolithic, embedded HTML/JS client implementation on the C++ server
side.

### Server-side architecture (C++, already exists, not part of this repo)

- **`StreamHost`** (`Source/Core/Core/HW/GBAStreamHost.h/.cpp`): one instance per GC port set to
  "GBA (Client Stream)" (`CORE_GC_GBA_STREAM`). Runs on port 6801–6804, sends video (RGB565 +
  raw-deflate) and audio (PCM) to exactly one connected client, receives button states back and
  feeds them into the GBA pad via `ControllerEmu::SetInputOverrideFunction`.
- **`GBAStreamLobby`** (`GBAStreamLobby.h/.cpp`): reference-counted singleton server on a fixed
  port 6800, serves the picker page (P1–P4) regardless of which GC port is active.
- The shared client side currently lives as one large embedded HTML/JS string in
  `GBAStreamClientPage.h` (`kGBAStreamClientHtml`, a C++ `R"HTML(...)HTML"` raw string), served
  by both servers. No build step, no external JS dependencies (deliberate, to keep server-side
  overhead minimal).

### Wire protocol

See [`docs/protocol.md`](docs/protocol.md) — the single source of truth every client implements
against.

## Goal of this repo

Build a standalone, general client framework for this protocol — replacing/complementing the
embedded HTML/JS string on the server side.

## Architecture

Shared protocol/codec logic (WebSocket framing, raw-deflate inflate, RGB565 conversion, PCM
buffering, input encoding) lives in [`core/`](core/) as a portable C library. Each platform gets a
thin shell on top of that for networking, rendering, audio output, and input polling, since those
parts are fundamentally different per target platform:

| Directory | Target platform | Toolchain | Status |
|---|---|---|---|
| [`clients/android/`](clients/android/) | Android app | Android SDK/NDK | working demo (connect, video, audio, input) |
| [`clients/3ds/`](clients/3ds/) | Nintendo 3DS homebrew | devkitARM / libctru | working (connect, video, audio, input, discovery) |
| [`clients/switch/`](clients/switch/) | Nintendo Switch homebrew | devkitA64 / libnx | working (connect, video, audio, input, discovery) |
| [`clients/nds/`](clients/nds/) | Nintendo DS homebrew | devkitARM / libnds | feasibility test client, see [`docs/nds-feasibility.md`](docs/nds-feasibility.md) |
| [`clients/web/`](clients/web/) | Any browser | none (static HTML/WASM) | working (connect, video, audio, input) |

Shared logo/icon source for every client: [`assets/logo/`](assets/logo/).

## Localization

Every client's UI text comes from a single source of truth,
[`i18n/strings.json`](i18n/strings.json), generated per client by
[`i18n/generate.py`](i18n/generate.py) — run that script after editing
`strings.json` and commit the regenerated output (same pattern as the web
client's `finlink_core.js`, see [`clients/web/README.md`](clients/web/README.md)).
Supported languages: German, English, French, Italian, Spanish. Each client
auto-selects one from the platform's own system language, falling back to
English when that can't be determined or isn't one of the five, with a
manual override in that client's own Settings screen. A string missing a
translation for a given language falls back to English at generation time.
