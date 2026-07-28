#pragma once

#include <array>

#include <borealis.hpp>

#include "discovery.hpp"

// Landing screen (Menu/Settings/Player, same three-screen structure as
// clients/android/.../MenuActivity.kt): manual host entry + P1-P4 picker
// (poll GET /status on all four player ports), or LAN discovery (sweep the
// local subnet for a host answering on the lobby port). Picking a free P
// slot pushes PlayerActivity; the settings row pushes SettingsActivity.
// Neither owns any GbaSession -- that's entirely PlayerActivity's job.
//
// The P1-P4 buttons and discovered-host rows are all pre-created (as
// GONE) in createContentView() and only ever have their text/visibility/
// click action mutated afterwards -- never constructed via addView() at
// runtime. That's a deliberate workaround, not just style: constructing
// and adding brand new views to an already-laid-out, already-rendered
// tree (i.e. exactly what happens the moment "Verbinden"/"Netzwerk
// durchsuchen" finishes) reproducibly crashed inside the Switch's Mesa/
// OpenGL driver (see the commit this comment was introduced in for the
// crash report/backtrace).
class MenuActivity : public brls::Activity {
  public:
    ~MenuActivity() override;

    brls::View *createContentView() override;

  private:
    static constexpr int kPlayerSlotCount = 4;
    static constexpr int kMaxDiscoveredRows = 8;

    brls::InputCell *hostInput = nullptr;
    brls::Box *slotRow = nullptr;
    std::array<brls::Button *, kPlayerSlotCount> slotButtons {};
    brls::Label *statusLabel = nullptr;
    brls::Label *discoveryStatusLabel = nullptr;
    brls::Box *discoveredList = nullptr;
    std::array<brls::DetailCell *, kMaxDiscoveredRows> discoveredCells {};

    std::string lastSearchedHost;
    bool searching = false;

    // Started in createContentView(), stopped in the destructor -- unlike
    // the old subnet-sweep startDiscovery() this replaced, there's no
    // manual "search" trigger: a server announces itself via UDP beacon
    // roughly every 2s on its own (docs/protocol.md), so this just needs
    // to keep listening for as long as the activity is alive. Its
    // heartbeat callback (see discovery.hpp) refreshes discoveredCells via
    // brls::sync(), same pre-existing-GONE-views mutation pattern as
    // runSearch()'s slotButtons update below.
    discovery::BeaconListener beaconListener;

    void refreshDiscoveredCells();
    void runSearch(const std::string &host);
    void launchPlayer(const std::string &host, int port);
};
