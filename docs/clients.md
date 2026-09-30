# Client baseline behavior

Behavioral rules every Unison client implementation follows, regardless of platform or language —
distinct from [`docs/protocol.md`](protocol.md) (the wire format itself) and
[`docs/capabilities.md`](capabilities.md) (which features are actually implemented where). This
file is about *how* a conforming client should behave at points the wire protocol leaves up to the
client, where getting it wrong is an easy, easy-to-miss mistake to make independently in each
client's own codebase. Start a new `##` section per rule as they come up; each should say what the
rule is, why it exists (what goes wrong without it), and which clients currently follow it.

## Resolve `hello_ack.video_mode` from the real `hello.stream_type`, never guess beforehand

**Rule:** A client must not decide what to send as `hello_ack.video_mode` until it has actually
received the server's `hello` message and read its `stream_type` field. The decision has to happen
inside the handshake itself, at the point `hello` is parsed — not earlier, and not based on
whatever the UI/connect flow assumed the server's type would be before dialing it.

**Why:** `hello_ack.video_mode` is the client's per-stream-type preference (e.g. "tiles" for
`GC_GBA_LINK`'s native pixel art, "h264"/"h265" for everything else — see `docs/protocol.md`'s
`hello_ack` section and [`docs/capabilities.md`](capabilities.md) for which modes each server
actually honors). A connection reached via **discovery** (a UDP beacon broadcast, or a slot picker
for a known multi-slot type like `GC_GBA_LINK`) already knows the real `stream_type` before ever
calling `connect()`, so picking the right mode ahead of time works fine there. A connection reached
via **manual host:port entry** does not: the user only typed an address, and the real
`stream_type` isn't knowable until the server's own `hello` names it, mid-handshake. A client that
picks a single mode before connecting and uses it for both paths either has to guess for manual
entry (wrong for the 3-of-4 current server types that no longer support "tiles"/"legacy" at all,
see `capabilities.md`) or special-case the manual-entry path separately from discovery — both mean
manual entry and discovery negotiate *differently*, purely as an accident of when Kotlin/Swift/C++
code happens to decide the mode, not because the protocol requires it.

The fix removes the asymmetry instead of patching around it: every known `stream_type`'s own
preferred mode is built into one small map (`TYPE=mode,TYPE=mode,...`, e.g. Android's
`Prefs.videoModesByTypeSerialized()`) and handed to the connect layer *before* connecting, same as
before — but the actual handshake code (not the UI layer) is what picks the one real value out of
that map, at the exact moment it parses `hello.stream_type`, for *every* connection regardless of
how it was reached. A redirect hop (`session_ready.redirect`, see `docs/protocol.md`) re-resolves
against its own fresh `hello` on the next loop iteration, so the value actually sent always matches
whichever hop ends up serving data. A `stream_type` the map has no entry for (a server from a
future/unrecognized fork) falls back to `"h264"`, the safe default every client's own
`videoModeFor()`-equivalent already uses for an unrecognized type.

Whatever real `stream_type` the handshake resolved against should also be handed back up to the
UI layer (e.g. via the connected-callback), not re-derived from whatever the UI guessed before
connecting — needed for any comparison against what the server actually granted
(`session_ready.video_mode`, see `docs/protocol.md`'s "Video-mode fallback") to stay honest for a
manual connection too.

**Status per client** (see each client's own `Prefs`/handshake-equivalent for the concrete names):

| Client | Resolves after real `hello.stream_type`? | Notes |
|---|---|---|
| `clients/android/` | Yes | `Prefs.videoModesByTypeSerialized()` / `jni_bridge.c`'s `resolve_video_mode()` |
| `clients/3ds/` | Yes | `Prefs::videoModesByTypeSerialized()` / `session.cpp`'s `resolveVideoMode()` |
| `clients/ios/` | Yes | `Prefs.videoModesByTypeSerialized()` / `unison_native_bridge.c`'s `resolve_video_mode()` |
| `clients/web/` | Yes | Already correct by construction — `currentStreamType` starts empty and is only ever set from a parsed `hello`; `videoModeFor(streamType)` is only called after that (`index.html`) |
| `clients/switch/` | N/A | No manual-entry-to-an-unknown-type path exists at all: every connection is either a hardcoded `GC_GBA_LINK` probe or a beacon-derived type known before `connect()` is called, so the asymmetry this rule addresses can't occur here |
| `clients/nds/` | No | Single flat global (`g_prefVideoMode`), no per-type map, nothing persisted — explicitly out of scope for this rule given the client's minimal/experimental status (see its own README and `docs/nds-feasibility.md`) |
