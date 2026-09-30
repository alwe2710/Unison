# Feature/client capability matrix

Ground truth for which Unison features are actually implemented where —
kept here specifically because feature work has been landing unevenly
across the four host forks and five clients (see the CI plan this file
came from), and nothing else in the repo states this in one place.

There are two kinds of "not supported yet":
- **Not implemented at all** — the client never sends anything other than
  the empty default, or the host never encodes anything other than TILES.
- **Implemented but not machine-checkable from here** — the four host
  forks (Cemu, azahar, melonDS, dolphin-gba-stream) are separate repos,
  not checked out during Unison's own CI run, so their column below is
  maintained by hand; only the five `clients/` rows are asserted against
  actual source by `tools/check_capabilities.py` (wired into
  `.github/workflows/build.yml`).

## Video modes (`hello_ack.video_mode`, see `docs/protocol.md`)

| | tiles | legacy | h264 | h265 |
|---|---|---|---|---|
| **Hosts** | | | | |
| Cemu | ✅ | ✅ | ✅ | ✅ |
| azahar | ❌ (hardcodes `format = 0`, see `bottom_screen_stream.cpp`) | ✅ | ❌ | ❌ |
| melonDS | ❌ (`video_encode.c` isn't even vendored into `src/unison/`) | ✅ | ❌ | ❌ |
| dolphin-gba-stream | ✅ (own independent tile-diff impl, not `unison_core`'s — see `GBAStreamHost.cpp`'s `VIDEO_FORMAT_TILES`) | ✅ | ❌ | ❌ |
| **Clients** | | | | |
| android | ✅ | ✅ | ✅ | ✅ |
| 3ds | ✅ | ❌ | ✅ (New3DS-only, MVD hardware decoder — `h264_decoder.cpp`; a base/Old 3DS has no such hardware and falls back, but that's a device capability, not a client one) | ❌ (the MVD hardware decoder only ever supports H.264; software HEVC decode on the 3DS's ARM11 CPU isn't remotely practical — see `session.hpp`'s own comment) |
| switch | ✅ | ❌ | ✅ (software decode via ffmpeg/libavcodec, `h264_decoder.cpp` — **TODO**: hardware decode (libnx's NVDEC) instead of software, not yet started) | ✅ (same ffmpeg path, `isH265` flag — same hardware-decode TODO as h264) |
| nds | ✅ (picker exists and lets you select h264/h265 as a *request*, but nothing on this client can decode either if a server actually granted it — `unison_decode_video_frame()` in `core/` rejects any format but TILES/INDEXED outright; this row is about actual decode capability, not what the picker merely lets you ask for) | ❌ | ❌ | ❌ |
| web | ✅ (default only, no picker) | ❌ | ❌ | ❌ |

A client with no picker at all (currently just web, per its own row above)
never sets `hello_ack.video_mode` (empty string, per `unison/handshake.h`'s
own comment on the field). What that empty string leads to depends
entirely on the host, verified per-host rather than assumed uniform: Cemu
explicitly parses `video_mode` and falls back to its own default ("tiles")
for anything it doesn't recognize (`UnisonMessages.cpp`'s `ParseHelloAck`);
azahar and melonDS never reference `video_mode`/`videoMode` anywhere in
their streaming code at all — they always send full raw frames
(`format = 0`) unconditionally, regardless of what any client requests.
"tiles" is ✅ for every *client* row specifically because decoding is
generic, shared `unison_core` logic, unconditional on the client side —
not because every host actually sends it (see the very different Hosts
row above it, where two of four can't encode tiles at all). A client
*with* a picker (3ds/switch/nds, all three per this table) can still
request a mode it can't itself decode — nds's row above is the concrete
example of exactly that gap.

## Machine-readable source (parsed by `tools/check_capabilities.py`)

Only the `clients` block is asserted against real source (see each
client's `source_glob`/`grep_for`); the `hosts` block is informational —
edit it by hand when a host fork's Unison integration changes, there is
currently no automated cross-repo check for it (see "Explicitly zurückgestellt"
in the CI plan — a checked-out multi-repo comparison is future work, not
this pass).

```json
{
  "clients": {
    "android": {
      "source_glob": "clients/android/app/src/main/java/com/unison/android/Prefs.kt",
      "extract": "video_mode_option_kotlin",
      "video_modes": ["tiles", "h264", "h265", "legacy"]
    },
    "3ds": {
      "source_glob": "clients/3ds/source/**",
      "extract": "format_dispatch_h264_h265",
      "video_modes": ["tiles", "h264"]
    },
    "switch": {
      "source_glob": "clients/switch/source/**",
      "extract": "format_dispatch_h264_h265",
      "video_modes": ["tiles", "h264", "h265"]
    },
    "nds": {
      "source_glob": "clients/nds/arm9/source/**",
      "extract": "format_dispatch_h264_h265",
      "video_modes": ["tiles"]
    },
    "web": {
      "source_glob": "clients/web/*.c",
      "extract": "grep_h264_h265",
      "video_modes": ["tiles"]
    }
  },
  "hosts": {
    "Cemu": { "video_modes": ["tiles", "legacy", "h264", "h265"] },
    "azahar": { "video_modes": ["legacy"] },
    "melonDS": { "video_modes": ["legacy"] },
    "dolphin-gba-stream": { "video_modes": ["tiles", "legacy"] }
  }
}
```
