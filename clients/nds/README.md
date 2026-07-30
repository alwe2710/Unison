# clients/nds

Nintendo DS homebrew client (devkitARM / libnds / dswifi). **Feasibility
test**, not a full-featured player: the NDS's Wi-Fi hardware is capped at
802.11b (1-2 Mbit/s), which is tight with the current wire protocol even
after the TILES compression change, purely by the numbers — see
[`docs/nds-feasibility.md`](../../docs/nds-feasibility.md). This client
exists to verify that on real hardware instead of only estimating it
theoretically.

## Scope

- Connects over Wi-Fi (`Wifi_InitDefault(WFC_CONNECT)`, using the WFC
  credentials stored in the console's system settings), then continuously
  displays servers that announce themselves via UDP discovery beacon (port
  6805, `docs/protocol.md` "Discovery-Beacon (UDP)" -- see
  [`arm9/source/beacon_discovery.c`](arm9/source/beacon_discovery.c)), and
  after picking a server (A/B/X/Y) goes through slot selection (also
  A/B/X/Y = slot 1-4) to a `StreamHost` port. If no server has announced
  itself yet, an on-screen keyboard (`nds/arm9/keyboard.h`, same software
  keyboard as the other three clients' host entry) asks for the IP; SELECT
  in the slot menu goes back to the server list, R re-prompts for the IP
  directly. No persistence across restarts -- same as the other three
  clients.
- Decodes video **and** audio with the same `core/` as every other client
  (WS handshake/framing, deflate, TILES/INDEXED formats).
- Displays video directly (main screen, `MODE_FB0`, 240×160 centered in
  256×192) and runs a live throughput/frame-rate readout on the bottom
  screen (console text).
- **Audio playback** via maxmod9 (`mmStreamOpen()`, manual mode, see
  `main()`/`audioStreamRequest()` in
  [`arm9/source/main.c`](arm9/source/main.c)) -- uses an `mmInstall()`
  call already present in the unmodified ARM7 core, no ARM7 change needed.
  Explicitly requests mono in the handshake (`max_channels = 1`) to save
  the tight bandwidth budget; the sample rate always stays native
  server-side (no downsampling, see `docs/protocol.md`).
- **GBA buttons are sent** (D-pad/A/B/L/R/Select/Start, 1:1 like a real
  GBA -- `buildGbaKeyMask()`/`sendGbaInput()` in
  [`arm9/source/main.c`](arm9/source/main.c)). **Holding X+Y together for
  ~0.6s** disconnects (like `clients/switch`'s ZL+ZR hold) -- plain
  START/SELECT won't do, since during an active session those are real,
  sendable GBA buttons, and unlike Switch/3DS the NDS has neither a HOME
  button nor a spare touch area for "Disconnect" without covering the
  bottom screen's throughput readout.

## Localization

The slot-selection screen's LEFT button opens `languageMenu()`, a
cursor-navigable list (UP/DOWN + A to confirm, B to cancel) -- the closest
equivalent this button-driven client (no touch-driven menus) has to every
other client's "tap a row, pick from it" language screen. Options are
System plus every language in
[`i18n/strings.json`](../../i18n/strings.json), alphabetically sorted by
their displayed (endonym) label. Confirming calls `applyLanguage()`, which
repoints every `STR_*` constant (`strings_generated.h`) via
`strSetLanguage()`, and the caller re-renders itself fully to pick up the
new language on every line. `applyLanguage()`'s System case reads
`PersonalData->language` (libnds `nds/system.h`) directly, falling back to
English for any value it has no translation for (Japanese, Chinese, or the
call simply being unavailable).

## Configuring the server IP

No compile-time `#define` needed anymore -- automatic discovery (see
"Scope" above) shows servers that announce themselves via UDP beacon, and
if none appear, an on-screen keyboard asks for one (`promptForIp()` in
[`arm9/source/main.c`](arm9/source/main.c)). Must be a literal IPv4
address, not a hostname -- the client deliberately uses `inet_addr()`
instead of `gethostbyname()`, to avoid a DNS round-trip over WFC; an
invalid entry simply results in a failed connection attempt, same as the
other three clients, there's no format validation.

## Building

Needs the same devkitARM toolchain as the 3DS client, plus `libnds`,
`dswifi`, `maxmod-nds`, `calico`, and `ndstool` (all from the normal
devkitPro pacman repo).

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM
cd clients/nds
make
```

Produces `finlink-nds.nds`. Two separately built ELFs (ARM9 + ARM7),
combined via `ndstool` -- devkitPro's `NDS.cmake` has **no** helper for
this (unlike Switch/3DS/Android), and even devkitPro's own reference
example for this case (`templates/combined`) uses classic Makefiles
instead of CMake. That's why this client deliberately deviates from the
other three clients' CMake pattern:

- `arm9/` -- the actual finlink client (`arm9/source/main.c`), links
  `finlink_core` in directly as source files (no `add_subdirectory()`
  equivalent in a classic Makefile).
- `arm7/` -- unmodified "default ARM7 core" from devkitPro's
  `templates/combined` example: NVRAM, extended keypad, RTC, power
  management, touch, sound/mic, and -- the part relevant to this ARM9
  side's `dswifi9` -- the wireless manager server
  (`wlmgrStartServer()`). No finlink-specific code needed.

(`calico`'s `ds_rules` does have a built-in default-ARM7 mechanism for
exactly this case -- linking a prebuilt `calico/bin/ds7_maine.elf` instead
of building your own ARM7 ELF -- but the `calico` package installed in
this environment shipped no `bin/` binaries, only `lib/`/`include/`/
`share/`. The two-ELF route via `templates/combined` sidesteps that and is
additionally the path devkitPro itself documents.)

## Known limitations

- Only GBA resolution 240×160 is supported (hardcoded for static buffer
  sizes instead of malloc/realloc, see comments in `main.c`) -- a frame
  with a different resolution is counted as an error, not displayed.
- No error handling for a receive buffer filling up beyond a single
  (even uncompressed) video frame -- with a broken/malicious server the
  connection is dropped instead of overflowing.
- No menu/settings/GBA-button overlay like the other three clients have
  (yet) -- see "Scope" above.
