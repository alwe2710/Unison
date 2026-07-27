#include "prefs.hpp"

#include <fstream>
#include <map>
#include <string>
#include <sys/stat.h>

namespace {
constexpr const char *kDir = "sdmc:/3ds/finlink";
constexpr const char *kFile = "sdmc:/3ds/finlink/settings.cfg";
} // namespace

Prefs::Prefs() {
    load();
}

void Prefs::load() {
    std::ifstream in(kFile);
    if (!in.is_open()) {
        return;
    }
    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        values[line.substr(0, eq)] = line.substr(eq + 1);
    }

    auto it = values.find("bilinear_video_filter");
    if (it != values.end()) {
        bilinearVideoFilter = it->second == "1";
    }
    it = values.find("bottom_screen_video");
    if (it != values.end()) {
        bottomScreenVideo = it->second == "1";
    }
}

void Prefs::save() {
    mkdir(kDir, 0777);
    std::ofstream out(kFile, std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    out << "bilinear_video_filter=" << (bilinearVideoFilter ? "1" : "0") << "\n";
    out << "bottom_screen_video=" << (bottomScreenVideo ? "1" : "0") << "\n";
}
