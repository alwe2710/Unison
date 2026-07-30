# core

Portable C99 library (CMake) with the platform-independent protocol/codec/transport logic, see
[`docs/protocol.md`](../docs/protocol.md). Pure buffer-in/buffer-out logic, no socket I/O — the
raw TCP handling (connect, send/recv) is each platform's own job in [`../clients/`](../clients/),
since that part is unavoidably platform-specific anyway (BSD sockets via libctru/libnx vs. Android
APIs). Everything that's pure logic on top of those bytes — including the WebSocket handshake and
framing itself — lives here, since 3DS/Switch homebrew ship no built-in WebSocket client at all.

- `include/finlink/websocket.h` + `src/websocket.c` — RFC6455 client handshake (key generation,
  request construction, accept validation) and frame reader/writer, matched to the server-side
  behavior in `GBAStreamHost.cpp` (unmasked, unfragmented server frames; masked client frames; no
  ping/pong, no permessage-deflate). Uses vendored `teeny-sha1` (MIT, see
  `third_party/teeny-sha1/LICENSE`) for the SHA1 in the handshake.
- `include/finlink/protocol.h` + `src/protocol.c` — (de)serialization of the three message types
  (video header, audio frame, input bitmask) within a WebSocket frame payload.
- `include/finlink/inflate.h` + `src/inflate.c` — raw-deflate inflate of the video payload, a
  wrapper around vendored `tinfl` (miniz, MIT, see `third_party/miniz/LICENSE`).
- `include/finlink/endian.h` — portable little-endian reads/writes for the wire format.

RGB565 conversion, PCM audio buffering, and input polling are deliberately not part of core —
those depend on each platform's own rendering/audio APIs. Where the random bytes needed for the
handshake and frame masking come from is likewise up to the caller (hardware TRNG through `rand()`)
rather than fixed inside core — the platforms differ too much here to usefully standardize on one
source.

## Building (host, for development/testing)

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Client builds pull in this library via `add_subdirectory(../../core)` instead of configuring it
standalone (tests are then automatically skipped).
