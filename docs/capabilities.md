# Feature/client capability matrix

Ground truth for which finlink features are actually implemented where —
kept here specifically because feature work has been landing unevenly
across the four host forks and five clients (see the CI plan this file
came from), and nothing else in the repo states this in one place.

There are two kinds of "not supported yet":
- **Not implemented at all** — the client never sends anything other than
  the empty default, or the host never encodes anything other than TILES.
- **Implemented but not machine-checkable from here** — the four host
  forks (Cemu, azahar, melonDS, dolphin-gba-stream) are separate repos,
  not checked out during finlink's own CI run, so their column below is
  maintained by hand; only the five `clients/` rows are asserted against
  actual source by `tools/check_capabilities.py` (wired into
  `.github/workflows/build.yml`).

## Video modes (`hello_ack.video_mode`, see `docs/protocol.md`)

| | tiles | legacy | h264 | h265 |
|---|---|---|---|---|
| **Hosts** | | | | |
| Cemu | ✅ | ✅ | ✅ | ✅ |
| azahar | ❌ (hardcodes `format = 0`, see `bottom_screen_stream.cpp`) | ✅ | ❌ | ❌ |
| melonDS | ❌ (`video_encode.c` isn't even vendored into `src/finlink/`) | ✅ | ❌ | ❌ |
| dolphin-gba-stream | ✅ (own independent tile-diff impl, not `finlink_core`'s — see `GBAStreamHost.cpp`'s `VIDEO_FORMAT_TILES`) | ✅ | ❌ | ❌ |
| **Clients** | | | | |
| android | ✅ | ✅ | ✅ | ✅ |
| 3ds | ✅ (default only, no picker) | ❌ | ❌ | ❌ |
| switch | ✅ (default only, no picker) | ❌ | ❌ | ❌ |
| nds | ✅ (default only, no picker) | ❌ | ❌ | ❌ |
| web | ✅ (default only, no picker) | ❌ | ❌ | ❌ |

A client with no picker never sets `hello_ack.video_mode` at all (empty
string, per `finlink/handshake.h`'s own comment on the field). What that
empty string leads to depends entirely on the host, verified per-host
rather than assumed uniform: Cemu explicitly parses `video_mode` and falls
back to its own default ("tiles") for anything it doesn't recognize
(`FinlinkMessages.cpp`'s `ParseHelloAck`); azahar and melonDS never
reference `video_mode`/`videoMode` anywhere in their streaming code at all
— they always send full raw frames (`format = 0`) unconditionally,
regardless of what any client requests. "tiles" is ✅ for every *client*
row specifically because decoding is generic, shared `finlink_core` logic,
unconditional on the client side — not because every host actually sends
it (see the very different Hosts row above it, where two of four can't
encode tiles at all).

## Machine-readable source (parsed by `tools/check_capabilities.py`)

Only the `clients` block is asserted against real source (see each
client's `source_glob`/`grep_for`); the `hosts` block is informational —
edit it by hand when a host fork's finlink integration changes, there is
currently no automated cross-repo check for it (see "Explicitly zurückgestellt"
in the CI plan — a checked-out multi-repo comparison is future work, not
this pass).

```json
{
  "clients": {
    "android": {
      "source_glob": "clients/android/app/src/main/java/com/finlink/android/Prefs.kt",
      "extract": "video_mode_option_kotlin",
      "video_modes": ["tiles", "h264", "h265", "legacy"]
    },
    "3ds": {
      "source_glob": "clients/3ds/source/**",
      "extract": "grep_h264_h265",
      "video_modes": ["tiles"]
    },
    "switch": {
      "source_glob": "clients/switch/source/**",
      "extract": "grep_h264_h265",
      "video_modes": ["tiles"]
    },
    "nds": {
      "source_glob": "clients/nds/arm9/source/**",
      "extract": "grep_h264_h265",
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
