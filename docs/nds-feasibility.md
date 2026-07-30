# NDS client: feasibility analysis (as of 2026-07-25)

Status: **being verified on real hardware**. Android, 3DS, and Switch came
first; the analysis below started out purely theoretical (no NDS hardware
available in the development environment). Since the TILES protocol change
(see [`protocol.md`](./protocol.md)), there's also
[`clients/nds`](../clients/nds) as a deliberately minimal test client (no
menu/player, just connect + decode + throughput/frame-rate readout), to
check the numbers below against a real server and real Wi-Fi conditions
instead of continuing to only estimate them.

## Core problem: Wi-Fi hardware limit

The NDS's Wi-Fi hardware only supports the 802.11b transfer rates **1 and
2 Mbit/s** (no 5.5/11 Mbit/s). Real TCP throughput over `dswifi` is even
lower than that due to protocol overhead. This is a hard hardware limit,
not a software weakness.

## Bandwidth budget against the current protocol

- **Audio alone**: e.g. 32 kHz, stereo, 16-bit PCM → 128,000 B/s ≈
  **1.02 Mbit/s**. That already matches the NDS's entire realistic Wi-Fi
  capacity -- before video even enters the picture.
- **Video**: 240×160 × RGB565 = 76,800 B/frame uncompressed, full frame
  (no TILES). Even with an optimistic 3-4× deflate compression, ~19-25
  kB/frame remain -- at 60 fps that alone is already ~9-12 Mbit/s, far
  above the hardware limit.

### With the TILES protocol change (see `protocol.md`)

Since the `format` bitmask (INDEXED/TILES), the server only has to send
the 8×8 tiles that changed since the last frame actually sent, per frame --
for GBA games with typically only partially moving image content (HUD/
still-image portions, text boxes, slower genres) this noticeably reduces
video bandwidth compared to the full-frame case, roughly estimated at 5-8×
at moderate motion (heavily content-dependent -- fast full-screen scrollers
benefit much less than, say, turn-based games). By the numbers, video can
therefore potentially stay in the low single-to-double-digit kB/frame
range instead of ~20-25 kB -- but:

**Audio is completely unaffected by the TILES change** (it only applies to
video frames) and, at ~1.02 Mbit/s for 32 kHz/stereo, remains on its own
close to the entire realistic Wi-Fi capacity. So even a video stream
heavily reduced by TILES plus unchanged stereo audio remains, by the
numbers, tight to infeasible at full frame rate -- the TILES change shifts
the ratio (audio becomes the dominant factor rather than an equal one) but
doesn't solve the underlying problem by itself.

**Conclusion (still mostly theoretical, see status above)**: a full
original stream (stereo audio + native frame rate) remains, by the
numbers, **tight to infeasible** on NDS with the current wire protocol,
even after TILES. What actually arrives on real hardware (among other
things because real `dswifi`/TCP overhead and actual tile-change rates are
hard to predict precisely) is exactly the question
[`clients/nds`](../clients/nds) is now meant to answer empirically.

## Precedents

Known NDS homebrew streaming projects (e.g. `streamer-ds`) solve the
comparable problem only through drastically reduced resolution/frame rate
and LZ77 compression -- no case of full-quality streaming over NDS Wi-Fi
was found.

## Options for a later NDS client

1. **No/minimal audio** (e.g. 8 kHz mono ≈ 128 kbit/s) + reduced frame
   rate (estimate: single-digit fps range, depending on actual
   compression achieved).
2. **Server-side protocol extension** for quality/frame-rate negotiation
   (a change to the `dolphin-gba-stream` fork, outside this repo). Since
   `protocol_version = 2`, this mechanism already exists generically in
   the handshake (see
   [`protocol.md`](./protocol.md#connection-setup-handshake), the section
   on `video_limits`/`audio_limits` and downscaling) -- currently relevant
   for NDS only insofar as `NDS_BOTTOM_SCREEN` is already reserved there
   as a `stream_type`. Whether an NDS client could use the frame-rate/
   resolution reduction this enables to actually reach the feasible range
   remains open, and additionally depends on option 1 (no/minimal audio),
   since `NDS_BOTTOM_SCREEN` is specified without audio transmission
   anyway.
3. NDS **not** as a live-stream client, but a reduced use case (e.g. only
   a status display/lobby via `/status`, no video/audio).

This analysis still rests on documented hardware limits, not a test on
real hardware (still unavailable in this development environment). The
devkitARM/libnds/dswifi toolchain part is no longer an obstacle at this
point -- [`clients/nds`](../clients/nds) builds and runs (verified as a
`.nds` ROM) -- but the actual number that matters (real throughput over
real Wi-Fi to a real finlink server) can only be measured on real
hardware. That result should be awaited before a final decision between
the options above.

## Sources

- [DSWifi documentation – BlocksDS](https://blocksds.skylyrac.net/dswifi/)
- [Wi-Fi – BlocksDS Tutorial](https://blocksds.skylyrac.net/tutorial/advanced/wifi/)
- [streamer-ds – GameBrew](https://www.gamebrew.org/wiki/Streamer-ds)
