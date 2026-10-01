# Project status (handoff notes)

Snapshot for picking this project back up in a fresh session — not a permanent doc, update or
delete sections as they go stale. See [`docs/protocol.md`](protocol.md) (wire format),
[`docs/capabilities.md`](capabilities.md) (per-client feature matrix, machine-checked), and
[`docs/clients.md`](clients.md) (cross-client behavioral rules) for the actual reference docs this
summarizes pointers to.

## `protocol_version = 4` (dedicated UDP video/audio channel) rollout

All four server forks (Cemu/`WIIU_GAMEPAD`, Azahar/`N3DS_BOTTOM_SCREEN`, melonDS/`NDS_BOTTOM_SCREEN`,
dolphin-gba-stream/`GC_GBA_LINK`) are already on `4`.

**Done and live-verified:** Android, Switch, 3DS.

3DS was verified today (2026-10-01): built with the local devkitARM toolchain (see "Local dev
toolchains" below), ran under a local Azahar build, connected to a live Cemu session. Confirmed via
a temporary diagnostic log (since removed) that UDP fragment reassembly and video-header parsing
work correctly end to end — then found H.264 decode itself hangs inside `mvdstdInit()` (the New3DS
MVD hardware IPC call never returns), which is Azahar's own incomplete MVD hardware emulation, not
a code bug. Real New3DS hardware remains unverified for the H.264-decode half specifically; the
channel adoption itself (the actual protocol_version 4 scope) is confirmed working.

**⚠️ High-value lead, not yet investigated:** `docs/protocol.md` currently says only iOS/NDS/Web are
still on v2 — but a quick grep today found both **iOS** (`clients/ios/Sources/Native/unison_native_bridge.c`)
and **NDS** (`clients/nds/arm9/source/main.c`) already have real-looking `udp_rendezvous()`/
`video_port`/`has_video_port` plumbing, both built against the shared `core/` `UNISON_PROTOCOL_VERSION`
(`4`) rather than a hardcoded older value. This is the *exact same pattern* 3DS turned out to be in
(commit 07f0dc5, code done months ago, doc never updated, never live-tested) — strongly worth
checking whether iOS/NDS are *also* already done before assuming either needs new implementation
work. For context, this session also found iOS's own README.md had a similarly stale "not built yet"
claim about a different feature (the whole Swift↔`unison_core` bridge) that turned out to already
be fully implemented — there may be a pattern of this project's own docs lagging well behind its
code across multiple features, not just this one.

If iOS/NDS turn out to already be code-complete too: live-test the same way 3DS was tested today
(see "How to live-test a client" below), then fix `docs/protocol.md`'s stale claims the same way
this session did for 3DS (grep that file for "iOS, NDS/DSi, Web" and similar phrasing — there were
*two* independently stale mentions found and fixed today, check for a third).

**Confirmed genuinely still v2:** Web (`clients/web/index.html` hardcodes
`const UNISON_PROTOCOL_VERSION = 2;` — real work needed there, not just a doc fix).

## Other known open items

- **Switch: software-only H.264/H.265 decode (ffmpeg/libavcodec), no hardware decode (NVDEC via
  libnx) yet.** Noted as a TODO annotation in `docs/capabilities.md`'s video-modes table (switch
  row) — not started.
- **Web client's own entry in `docs/capabilities.md`'s machine-readable block was left untouched**
  this session (still uses the older, less precise `grep_h264_h265` check strategy, not the newer
  `format_dispatch_h264_h265` one 3ds/switch/nds use) — not investigated for the same false-positive
  risk the older strategy had elsewhere; see `tools/check_capabilities.py`'s own docstring for what
  the two strategies actually check.

## CI (`.github/workflows/build.yml`) — two known-red jobs, unrelated to recent work

As of commit `af599d7` (2026-10-01), `main`'s build workflow has two consistently failing jobs,
both pre-existing/infra-level, not caused by this session's changes (the `3ds (.3dsx)` and
`capability matrix lint` jobs are green again after today's fixes):

- **`android (assembleDebug)`** fails at the `android-actions/setup-android@v3` step itself
  (`sdkmanager failed to find package 'tools'`), before Gradle ever runs — looks like a runner-image/
  Android SDK Preview licensing issue, not an app-code problem. Not investigated further.
- **`ios (XcodeGen + xcodebuild, Simulator)`** fails in `Run UnisonTests`, specifically
  `BeaconListenerTests.testReceivesAndParsesARealBeaconPacket` (a real UDP socket bound inside the
  Simulator sandbox) — `GbaStreamClientTests` (this session's own test-signature fix) passes fine in
  the same run, so this looks unrelated to recent changes. `build.yml`'s own comments already flag
  this general class of flakiness (two test bundles both binding the same fixed UDP port
  concurrently caused an `EADDRINUSE` once before) — worth checking whether this is the same root
  cause recurring, or something new.

## Local dev toolchains available in this environment (not obvious, worth knowing up front)

- **devkitARM** (3DS toolchain): not at the usual `/opt/devkitpro` — installed under
  `/home/alex/devkitpro-local/root/opt/devkitpro` instead. Export
  `DEVKITPRO=/home/alex/devkitpro-local/root/opt/devkitpro` and
  `DEVKITARM=$DEVKITPRO/devkitARM` (plus `PATH+=$DEVKITARM/bin:$DEVKITPRO/tools/bin`) before running
  the same `cmake -S clients/3ds -B build/3ds -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake`
  sequence `.github/workflows/build.yml`'s `build-3ds` job uses. No devkitA64 (Switch) or Xcode/macOS
  toolchain confirmed available locally the same way — check before assuming either is missing.
- **A local Azahar build** exists at
  `/run/media/alex/of/emulation-companion/azahar/build/bin/Release/azahar` — doubles as a 3DS
  homebrew runner (it's a Citra fork, loads `.3dsx` files directly via `-w <path>` CLI arg or
  File → Load File) for live-testing the 3DS client without needing real hardware or a separate
  Citra install. `-f` (fullscreen) crashes immediately with a Wayland protocol error on this desktop
  (`wp_fifo_manager_v1` error) — use `-w` (windowed) instead.
- **A local Cemu build** exists and already has Unison enabled (`~/.config/Cemu/settings.xml`,
  `UnisonPort=6840`) — launch with
  `Cemu_release -g "<path>/THE LEGEND OF ZELDA The Wind Waker HD [BCZP]/code/cking.rpx" --verbose`
  from `/run/media/alex/of/emulation-companion/Cemu/bin/`. Has occasionally segfaulted on the very
  first launch attempt of a session for no clear reason (disk space was *not* the cause, confirmed
  live) — a second launch attempt right after has always succeeded so far.

### How to live-test a client (the pattern used for 3DS today)

1. Build the client for real (see toolchain notes above).
2. Launch a real server (Cemu for `WIIU_GAMEPAD` is the most battle-tested option this session —
   settings already configured).
3. Run the client, connect (manual `127.0.0.1:<port>` works fine, everything local, no Wi-Fi
   variables).
4. If something doesn't work and it's unclear why: add a *temporary* SD-card/file diagnostic log at
   the exact point needed (see `h264_decoder.cpp`'s `MvdLog` for the established pattern/precedent
   in this codebase: open-append-flush-close every call, so the last line written survives even a
   subsequent hang/crash) — remove it again once the question is answered, same discipline every
   existing temporary log in this codebase already follows.
5. This desktop has no Wayland input-injection tool available (`xdotool`/`ydotool`/`wtype` all
   absent) — clicking/typing into an emulator window needs the human at the keyboard; screenshots
   via `spectacle -b -n -o <path>` work fine for verifying state without needing to interact.

## Disk space (was critical earlier this session, is fine now)

`/run/media/alex/of` (where Unison/Azahar/Cemu/dolphin-gba-stream all live) was at 100% (4.0G free
of 906G) for most of this session — someone freed space mid-session (jumped to 44G free). Still
worth a quick `df -h /run/media/alex/of /home` before anything build-heavy; it's a real constraint
on this machine, not a one-off.
