#include "menu_activity.hpp"

#include <algorithm>

#include "discovery.hpp"
#include "player_activity.hpp"
#include "settings_activity.hpp"
#include "strings_generated.hpp"
#include "thread_utils.hpp"

namespace {

constexpr int kPlayerBasePort = 6801;

// The one stream type with a lobby port fanning out to separate per-slot
// ports (see runSearch() below) -- every other stream type is single-client
// and its beacon's handshakePort *is* the only port there is, so a
// discovered entry for one of those connects to it directly instead of
// running the GC_GBA_LINK-specific slot probe (which always reported every
// slot unreachable for a server that was never Dolphin).
constexpr const char *kStreamTypeGcGbaLink = "GC_GBA_LINK";

void addDivider(brls::Box *parent) {
    auto *rect = new brls::Rectangle(nvgRGBA(255, 255, 255, 40));
    rect->setWidthPercentage(100);
    rect->setHeight(1);
    rect->setMarginTop(12);
    rect->setMarginBottom(12);
    parent->addView(rect);
}

} // namespace

brls::View *MenuActivity::createContentView() {
    auto *column = new brls::Box();
    column->setAxis(brls::Axis::COLUMN);
    column->setAlignItems(brls::AlignItems::STRETCH);
    column->setPadding(24, 32, 24, 32);

    hostInput = new brls::InputCell();
    hostInput->init(
        "Host", "", [](std::string) {}, strings::kHostHintExample, "IP-Adresse des Streaming-Hosts");
    column->addView(hostInput);

    auto *connectCell = new brls::DetailCell();
    connectCell->setText(strings::kMenuConnect);
    connectCell->registerClickAction([this](brls::View *) {
        std::string host = hostInput->getValue();
        if (host.empty()) {
            statusLabel->setText(strings::kLobbyHostRequired);
            return true;
        }
        runSearch(host);
        return true;
    });
    column->addView(connectCell);

    slotRow = new brls::Box();
    slotRow->setAxis(brls::Axis::ROW);
    slotRow->setMarginTop(8);
    for (int slot = 0; slot < kPlayerSlotCount; slot++) {
        auto *button = new brls::Button();
        // BUTTONSTYLE_DEFAULT (what a plain new Button() uses) has
        // identical enabled/disabled colors in borealis's built-in theme --
        // ButtonState::DISABLED works (registerClickAction stops firing),
        // it just looks the same. BUTTONSTYLE_PRIMARY is the one built-in
        // style that actually dims when disabled.
        button->setStyle(&brls::BUTTONSTYLE_PRIMARY);
        button->setVisibility(brls::Visibility::GONE);
        button->setText("P" + std::to_string(slot + 1));
        button->setGrow(1.0f);
        button->setMarginLeft(slot == 0 ? 0 : 8);
        slotRow->addView(button);
        slotButtons[slot] = button;
    }
    column->addView(slotRow);

    statusLabel = new brls::Label();
    statusLabel->setText(strings::kStatusDisconnected);
    statusLabel->setMarginTop(8);
    column->addView(statusLabel);

    addDivider(column);

    discoveryStatusLabel = new brls::Label();
    discoveryStatusLabel->setText(strings::kDiscoverySearchingPlaceholder);
    column->addView(discoveryStatusLabel);

    discoveredList = new brls::Box();
    discoveredList->setAxis(brls::Axis::COLUMN);
    discoveredList->setMarginTop(4);
    for (int i = 0; i < kMaxDiscoveredRows; i++) {
        auto *cell = new brls::DetailCell();
        cell->setVisibility(brls::Visibility::GONE);
        discoveredList->addView(cell);
        discoveredCells[i] = cell;
    }
    column->addView(discoveredList);

    addDivider(column);

    auto *settingsCell = new brls::DetailCell();
    settingsCell->setText(strings::kSettings);
    settingsCell->registerClickAction([](brls::View *) {
        brls::Application::pushActivity(new SettingsActivity());
        return true;
    });
    column->addView(settingsCell);

    auto *scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setContentView(column);

    auto *frame = new brls::AppletFrame(scroll);
    frame->setTitle(strings::kAppName);

    beaconListener.start([this]() { brls::sync([this]() { refreshDiscoveredCells(); }); });

    return frame;
}

MenuActivity::~MenuActivity() {
    beaconListener.stop();
}

void MenuActivity::refreshDiscoveredCells() {
    auto servers = beaconListener.snapshot();
    int shown = std::min(static_cast<int>(servers.size()), kMaxDiscoveredRows);
    for (int i = 0; i < kMaxDiscoveredRows; i++) {
        auto *cell = discoveredCells[i];
        if (i >= shown) {
            cell->setVisibility(brls::Visibility::GONE);
            continue;
        }
        const auto &srv = servers[i];
        std::string label = srv.gameTitle.empty() ? srv.host : srv.gameTitle;
        if (!srv.compatible) {
            label += strings::kDiscoveryIncompatibleSuffix;
        }
        cell->setText(label);
        cell->setVisibility(brls::Visibility::VISIBLE);
        // DetailCell has no enabled/disabled visual state the way Button
        // does (see this file's own top comment on BUTTONSTYLE_PRIMARY) --
        // an incompatible entry stays tappable, but connecting to it would
        // just fail the app-level handshake on a protocol_version mismatch
        // anyway, so this short-circuits that round trip with the same
        // message instead.
        bool compatible = srv.compatible;
        std::string host = srv.host;
        std::string streamType = srv.streamType;
        int handshakePort = srv.handshakePort;
        cell->registerClickAction([this, host, compatible, streamType, handshakePort](brls::View *) {
            if (!compatible) {
                statusLabel->setText("Inkompatible Protokollversion.");
                return true;
            }
            if (streamType == kStreamTypeGcGbaLink) {
                hostInput->setValue(host);
                runSearch(host);
            } else {
                launchPlayer(host, handshakePort, streamType);
            }
            return true;
        });
    }
    discoveryStatusLabel->setText(servers.empty() ? strings::kDiscoverySearchingPlaceholder : strings::kDiscoveryFoundHeader);
}

void MenuActivity::runSearch(const std::string &host) {
    if (searching) {
        return;
    }
    searching = true;
    for (auto *button : slotButtons) {
        button->setVisibility(brls::Visibility::GONE);
    }
    statusLabel->setText(strings::kDiscoveryScanning);

    thread_utils::spawnDetached([this, host]() {
        std::array<std::optional<bool>, kPlayerSlotCount> occupied;
        for (int slot = 0; slot < kPlayerSlotCount; slot++) {
            occupied[slot] = discovery::fetchOccupied(host, kPlayerBasePort + slot);
        }

        brls::sync([this, host, occupied]() {
            lastSearchedHost = host;
            searching = false;

            bool anyFree = false;
            for (int slot = 0; slot < kPlayerSlotCount; slot++) {
                auto *button = slotButtons[slot];
                button->setVisibility(brls::Visibility::VISIBLE);

                if (!occupied[slot].has_value() || *occupied[slot]) {
                    button->setState(brls::ButtonState::DISABLED);
                } else {
                    button->setState(brls::ButtonState::ENABLED);
                    anyFree = true;
                    int port = kPlayerBasePort + slot;
                    button->registerClickAction([this, host, port](brls::View *) {
                        launchPlayer(host, port, kStreamTypeGcGbaLink);
                        return true;
                    });
                }
            }

            statusLabel->setText(anyFree ? strings::kLobbyPick : strings::kLobbyNoneConfigured);
        });
    });
}

void MenuActivity::launchPlayer(const std::string &host, int port, const std::string &streamType) {
    brls::Application::pushActivity(new PlayerActivity(host, port, streamType));
}
