#include "menu_activity.hpp"

#include <algorithm>
#include <atomic>
#include <memory>

#include "discovery.hpp"
#include "player_activity.hpp"
#include "settings_activity.hpp"
#include "thread_utils.hpp"

namespace {

constexpr int kPlayerBasePort = 6801;

// Shared state for startDiscovery()'s worker pool -- kept alive by the
// shared_ptr each worker lambda captures for as long as any of them are
// still running.
struct DiscoveryState {
    std::vector<std::string> hosts;
    std::atomic<size_t> nextIndex { 0 };
    std::atomic<int> nextRow { 0 };
    std::atomic<int> completed { 0 };
    std::atomic<int> workersRemaining { 0 };
};

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
        "Host", "", [](std::string) {}, "z.B. 192.168.1.5", "IP-Adresse des Streaming-Hosts");
    column->addView(hostInput);

    auto *connectCell = new brls::DetailCell();
    connectCell->setText("Verbinden");
    connectCell->registerClickAction([this](brls::View *) {
        std::string host = hostInput->getValue();
        if (host.empty()) {
            statusLabel->setText("Bitte zuerst einen Host eingeben.");
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
    statusLabel->setText("Nicht verbunden.");
    statusLabel->setMarginTop(8);
    column->addView(statusLabel);

    addDivider(column);

    auto *discoverCell = new brls::DetailCell();
    discoverCell->setText("Netzwerk durchsuchen");
    discoverCell->registerClickAction([this](brls::View *) {
        startDiscovery();
        return true;
    });
    column->addView(discoverCell);

    discoveryProgress = new ProgressBar();
    discoveryProgress->setVisibility(brls::Visibility::GONE);
    discoveryProgress->setMarginTop(8);
    column->addView(discoveryProgress);

    discoveryStatusLabel = new brls::Label();
    discoveryStatusLabel->setText("");
    discoveryStatusLabel->setMarginTop(4);
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
    settingsCell->setText("Einstellungen");
    settingsCell->registerClickAction([](brls::View *) {
        brls::Application::pushActivity(new SettingsActivity());
        return true;
    });
    column->addView(settingsCell);

    auto *scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setContentView(column);

    auto *frame = new brls::AppletFrame(scroll);
    frame->setTitle("finlink");
    return frame;
}

void MenuActivity::runSearch(const std::string &host) {
    if (searching) {
        return;
    }
    searching = true;
    for (auto *button : slotButtons) {
        button->setVisibility(brls::Visibility::GONE);
    }
    statusLabel->setText("Suche...");

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
                        launchPlayer(host, port);
                        return true;
                    });
                }
            }

            statusLabel->setText(anyFree ? "Freien Slot wählen." : "Kein freier Slot auf diesem Host.");
        });
    });
}

void MenuActivity::startDiscovery() {
    if (discovering) {
        return;
    }
    discovering = true;
    for (auto *cell : discoveredCells) {
        cell->setVisibility(brls::Visibility::GONE);
    }
    discoveryProgress->setProgress(0.0f);
    discoveryProgress->setVisibility(brls::Visibility::VISIBLE);
    discoveryStatusLabel->setText("Suche läuft...");

    thread_utils::spawnDetached([this]() {
        auto hosts = discovery::localSubnetHosts();
        if (hosts.empty()) {
            brls::sync([this]() {
                discovering = false;
                discoveryProgress->setVisibility(brls::Visibility::GONE);
                discoveryStatusLabel->setText("Kein lokales Netzwerk gefunden.");
            });
            return;
        }

        // Probing all (up to 254) hosts one at a time would take far too
        // long -- split the range across a handful of worker threads
        // instead, same as clients/3ds/source/main.cpp's startDiscovery().
        constexpr int kWorkers = 8;
        auto state = std::make_shared<DiscoveryState>();
        state->hosts = std::move(hosts);
        state->workersRemaining = kWorkers;
        auto total = static_cast<float>(state->hosts.size());

        for (int w = 0; w < kWorkers; w++) {
            thread_utils::spawnDetached([this, state, total]() {
                for (;;) {
                    size_t i = state->nextIndex.fetch_add(1);
                    if (i >= state->hosts.size()) {
                        break;
                    }
                    const std::string &ip = state->hosts[i];
                    if (discovery::probeLobby(ip)) {
                        int row = state->nextRow.fetch_add(1);
                        if (row < kMaxDiscoveredRows) {
                            brls::sync([this, ip, row]() {
                                auto *cell = discoveredCells[row];
                                cell->setText(ip);
                                cell->setVisibility(brls::Visibility::VISIBLE);
                                cell->registerClickAction([this, ip](brls::View *) {
                                    hostInput->setValue(ip);
                                    runSearch(ip);
                                    return true;
                                });
                            });
                        }
                    }

                    int done = state->completed.fetch_add(1) + 1;
                    brls::sync([this, done, total]() {
                        discoveryProgress->setProgress(static_cast<float>(done) / total);
                    });
                }

                if (state->workersRemaining.fetch_sub(1) == 1) {
                    int shown = std::min(state->nextRow.load(), kMaxDiscoveredRows);
                    brls::sync([this, shown]() {
                        discovering = false;
                        discoveryProgress->setVisibility(brls::Visibility::GONE);
                        discoveryStatusLabel->setText(shown == 0 ? "Nichts gefunden." : "Suche abgeschlossen.");
                    });
                }
            });
        }
    });
}

void MenuActivity::launchPlayer(const std::string &host, int port) {
    brls::Application::pushActivity(new PlayerActivity(host, port));
}
