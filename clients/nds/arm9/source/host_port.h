#pragma once

// The "host" or "host:port" manual-entry parsing decision -- pulled out of
// main.c's promptForIp() into its own free function, free of <nds.h>
// (unlike the rest of main.c), so it has one place to unit-test on a plain
// host compiler instead of only being exercisable on hardware/melonDS --
// see tests/test_host_port.c. Same "host:port" convention every other
// client's manual entry now uses (see clients/3ds/source/host_port.hpp's
// comment, and Android's MenuActivity.searchLobby()): a colon means
// "connect directly to this single-slot server" (Cemu/Azahar/melonDS's
// WIIU_GAMEPAD/N3DS_BOTTOM_SCREEN/NDS_BOTTOM_SCREEN stream types use
// exactly this), bypassing the GC_GBA_LINK P1-P4 lobby picker entirely.

// Parses (and, on success, truncates in place) a trailing ":<port>" off
// `host`. Returns the parsed port (1-65535) on success; returns 0 and
// leaves `host` untouched if there was no valid trailing ":<port>" (no
// colon, a colon with nothing/non-digits after it, or an out-of-range
// port) -- callers then fall back to treating the whole string as a bare
// host, unchanged from before this existed.
int unisonNdsSplitHostPort(char *host);
