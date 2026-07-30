#include "prefs.hpp"

#include <fstream>
#include <sys/stat.h>

#include <switch.h>

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

    auto it = values.find("language");
    if (it != values.end()) {
        if (it->second == "de") {
            language = LanguagePref::DE;
        } else if (it->second == "en") {
            language = LanguagePref::EN;
        } else {
            language = LanguagePref::SYSTEM;
        }
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
    out << "language=" << (language == LanguagePref::DE ? "de" : language == LanguagePref::EN ? "en" : "system") << "\n";
}

strings::Lang resolveLanguage(const Prefs &prefs) {
    if (prefs.language == Prefs::LanguagePref::DE) {
        return strings::Lang::DE;
    }
    if (prefs.language == Prefs::LanguagePref::EN) {
        return strings::Lang::EN;
    }
    strings::Lang result = strings::Lang::EN;
    if (R_SUCCEEDED(setInitialize())) {
        u64 languageCode = 0;
        if (R_SUCCEEDED(setGetSystemLanguage(&languageCode))) {
            SetLanguage setLang;
            if (R_SUCCEEDED(setMakeLanguage(languageCode, &setLang)) && setLang == SetLanguage_DE) {
                result = strings::Lang::DE;
            }
        }
        setExit();
    }
    return result;
}

void applyLanguage(const Prefs &prefs) {
    strings::setLanguage(resolveLanguage(prefs));
}
