# Unison

[![build](https://github.com/alwe2710/Unison/actions/workflows/build.yml/badge.svg?branch=transcoding)](https://github.com/alwe2710/Unison/actions/workflows/build.yml?query=branch%3Atranscoding)

Generic client framework for the Unison streaming protocol: a small WebSocket-based protocol for
streaming a console's screen, audio, and input between an emulator and a remote client.

## Context

Unison is implemented by forks of four emulators, each streaming a different console/screen:

| Emulator fork | `stream_type` | What it streams |
|---|---|---|
| dolphin-gba-stream (Dolphin) | `GC_GBA_LINK` | GameCube's built-in GBA emulation (GC↔GBA Link Cable) |
| Cemu fork (`src/Cemu/unisonStream/`) | `WIIU_GAMEPAD` | Wii U GamePad screen |
| Azahar fork (`src/core/streaming/`) | `N3DS_BOTTOM_SCREEN` | 3DS bottom screen |
| melonDS fork (`src/streaming/`) | `NDS_BOTTOM_SCREEN` | DS bottom screen |

None of these forks are part of this repo. See [Stream Types](docs/protocol.md#stream-types) for
the full breakdown of what each one supports (slots, audio, microphone).

### Server-side architecture (each emulator fork, not part of this repo)

Every fork follows the same general shape, under its own project's naming:

- A **lobby** server on a fixed port 6800 handles the connection handshake (see
  [`docs/protocol.md`](docs/protocol.md#endpoints)), independent of which stream slot ends up
  serving the client. In dolphin-gba-stream, this is `GBAStreamLobby` (`GBAStreamLobby.h/.cpp`), a
  reference-counted singleton.
- A **stream host** server, one instance per active slot, sends video (RGB565 + raw-deflate) and
  audio (PCM, where the stream type has audio) to exactly one connected client and receives input
  back. In dolphin-gba-stream, this is `StreamHost` (`Source/Core/Core/HW/GBAStreamHost.h/.cpp`),
  one instance per GC port set to "GBA (Client Stream)" (`CORE_GC_GBA_STREAM`), running on ports
  6801–6804.

### Wire protocol

See [`docs/protocol.md`](docs/protocol.md) — the single source of truth every client and server
implements against.

## Goal of this repo

Provide a standalone, general client framework for this protocol, shared across all four emulator
forks.

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
client's `unison_core.js`, see [`clients/web/README.md`](clients/web/README.md)).
Supported languages: German, English, French, Italian, Spanish. Each client
auto-selects one from the platform's own system language, falling back to
English when that can't be determined or isn't one of the five, with a
manual override in that client's own Settings screen. A string missing a
translation for a given language falls back to English at generation time.
