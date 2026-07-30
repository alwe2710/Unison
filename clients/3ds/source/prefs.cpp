#include "prefs.hpp"

#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <sys/stat.h>

namespace {
constexpr const char *kDir = "sdmc:/3ds/finlink";
constexpr const char *kFile = "sdmc:/3ds/finlink/settings.cfg";
} // namespace

namespace {
// "bilinear_video_filter.GC_GBA_LINK" -- the prefix every per-stream-type
// key uses, so load()/save() can round-trip the whole map without a fixed
// list of known stream types (a server introducing a new one shouldn't
// need a client code change just to remember its filter preference).
constexpr const char *kBilinearKeyPrefix = "bilinear_video_filter.";

// See Prefs::bilinearFor()'s own comment: GC_GBA_LINK's GBA output is
// native-resolution pixel art (nearest-neighbor looks right), while
// WIIU_GAMEPAD/N3DS_BOTTOM_SCREEN/NDS_BOTTOM_SCREEN are already-upscaled/
// higher-effective-resolution renders that read better smoothed.
bool defaultBilinearFor(const std::string &streamType) {
    return streamType == "WIIU_GAMEPAD" || streamType == "N3DS_BOTTOM_SCREEN" || streamType == "NDS_BOTTOM_SCREEN";
}

// Keep in sync with i18n/strings.json's language set.
Prefs::LanguagePref languagePrefFromCode(const std::string &code) {
    if (code == "de") return Prefs::LanguagePref::DE;
    if (code == "en") return Prefs::LanguagePref::EN;
    if (code == "fr") return Prefs::LanguagePref::FR;
    if (code == "it") return Prefs::LanguagePref::IT;
    if (code == "es") return Prefs::LanguagePref::ES;
    return Prefs::LanguagePref::SYSTEM;
}

const char *languagePrefCode(Prefs::LanguagePref pref) {
    switch (pref) {
    case Prefs::LanguagePref::DE: return "de";
    case Prefs::LanguagePref::EN: return "en";
    case Prefs::LanguagePref::FR: return "fr";
    case Prefs::LanguagePref::IT: return "it";
    case Prefs::LanguagePref::ES: return "es";
    default: return "system";
    }
}
} // namespace

Prefs::Prefs() {
    load();
}

bool Prefs::bilinearFor(const std::string &streamType) const {
    auto it = bilinearVideoFilterByStreamType.find(streamType);
    if (it != bilinearVideoFilterByStreamType.end()) {
        return it->second;
    }
    return defaultBilinearFor(streamType);
}

void Prefs::setBilinearFor(const std::string &streamType, bool value) {
    bilinearVideoFilterByStreamType[streamType] = value;
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

    for (const auto &[key, value] : values) {
        if (key.rfind(kBilinearKeyPrefix, 0) == 0) {
            bilinearVideoFilterByStreamType[key.substr(strlen(kBilinearKeyPrefix))] = value == "1";
        }
    }
    auto it = values.find("bottom_screen_video");
    if (it != values.end()) {
        bottomScreenVideo = it->second == "1";
    }

    auto langIt = values.find("language");
    if (langIt != values.end()) {
        language = languagePrefFromCode(langIt->second);
    }
}

void Prefs::save() {
    mkdir(kDir, 0777);
    std::ofstream out(kFile, std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    for (const auto &[streamType, value] : bilinearVideoFilterByStreamType) {
        out << kBilinearKeyPrefix << streamType << "=" << (value ? "1" : "0") << "\n";
    }
    out << "bottom_screen_video=" << (bottomScreenVideo ? "1" : "0") << "\n";
    out << "language=" << languagePrefCode(language) << "\n";
}
