#include "prefs.hpp"

#include <fstream>
#include <sys/stat.h>

namespace {
constexpr const char *kDir = "sdmc:/switch/finlink";
constexpr const char *kFile = "sdmc:/switch/finlink/settings.cfg";
} // namespace

namespace {
// "bilinear_video_filter.GC_GBA_LINK" -- the prefix every per-stream-type
// key uses within `values` (round-tripped wholesale, see save() below), so
// a server introducing a new stream_type doesn't need a client code change
// just to remember its filter preference.
constexpr const char *kBilinearKeyPrefix = "bilinear_video_filter.";

// See Prefs::bilinearFor()'s own comment: GC_GBA_LINK's GBA output is
// native-resolution pixel art (nearest-neighbor looks right), while
// WIIU_GAMEPAD/N3DS_BOTTOM_SCREEN/NDS_BOTTOM_SCREEN are already-upscaled/
// higher-effective-resolution renders that read better smoothed.
bool defaultBilinearFor(const std::string &streamType) {
    return streamType == "WIIU_GAMEPAD" || streamType == "N3DS_BOTTOM_SCREEN" || streamType == "NDS_BOTTOM_SCREEN";
}
} // namespace

std::string Prefs::path() const {
    return kFile;
}

Prefs::Prefs() {
    load();
}

bool Prefs::bilinearFor(const std::string &streamType) const {
    auto it = values.find(kBilinearKeyPrefix + streamType);
    if (it != values.end()) {
        return it->second == "1";
    }
    return defaultBilinearFor(streamType);
}

void Prefs::setBilinearFor(const std::string &streamType, bool value) {
    values[kBilinearKeyPrefix + streamType] = value ? "1" : "0";
}

void Prefs::load() {
    std::ifstream in(path());
    if (!in.is_open()) {
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        values[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

void Prefs::save() {
    mkdir(kDir, 0777);

    std::ofstream out(path(), std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    for (const auto &[key, value] : values) {
        out << key << "=" << value << "\n";
    }
}
