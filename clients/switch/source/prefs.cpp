#include "prefs.hpp"

#include <fstream>
#include <sys/stat.h>

namespace {
constexpr const char *kDir = "sdmc:/switch/finlink";
constexpr const char *kFile = "sdmc:/switch/finlink/settings.cfg";
} // namespace

std::string Prefs::path() const {
    return kFile;
}

Prefs::Prefs() {
    load();
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

    auto it = values.find("bilinear_video_filter");
    if (it != values.end()) {
        bilinearVideoFilter = it->second == "1";
    }
}

void Prefs::save() {
    mkdir(kDir, 0777);

    values["bilinear_video_filter"] = bilinearVideoFilter ? "1" : "0";

    std::ofstream out(path(), std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    for (const auto &[key, value] : values) {
        out << key << "=" << value << "\n";
    }
}
