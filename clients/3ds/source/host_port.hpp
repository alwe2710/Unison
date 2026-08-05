#pragma once

#include <optional>
#include <string>

// The "host" or "host:port" manual-entry parsing decision -- pulled out of
// main.cpp's drawMenuScreen() into its own free function, free of <3ds.h>
// (unlike the rest of main.cpp), so it has one place to unit-test on a
// plain host compiler instead of only being exercisable on hardware/citra
// -- see tests/test_host_port.cpp. Same "host:port" convention Android's
// MenuActivity.searchLobby() already supports (see its own comment there,
// and strings.json's host_hint "IP address or IP:port"): a colon means
// "connect directly to this single-slot server" (Cemu/azahar/melonDS's
// WIIU_GAMEPAD/N3DS_BOTTOM_SCREEN/NDS_BOTTOM_SCREEN stream types use
// exactly this), bypassing the GC_GBA_LINK P1-P4 lobby probe entirely.

struct HostPort {
    std::string host;
    int port;
};

// nullopt when `raw` has no (valid) trailing ":<port>" -- callers then fall
// back to treating the whole string as a bare host for the P1-P4 lobby
// probe, unchanged from before this existed.
std::optional<HostPort> splitHostPort(const std::string &raw);
