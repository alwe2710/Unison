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
        language = languagePrefFromCode(it->second);
    }

    auto videoModeIt = values.find("video_mode");
    if (videoModeIt != values.end()) {
        videoMode = videoModeIt->second;
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
    out << "language=" << languagePrefCode(language) << "\n";
    out << "video_mode=" << videoMode << "\n";
}

// Prefs::LanguagePref::SYSTEM resolves to whichever of DE/FR/IT/ES the
// console's own system language (setGetSystemLanguage()) matches --
// anything else (including the call failing, or a system language this
// app has no translation for) falls back to English, same policy as every
// other client.
strings::Lang resolveLanguage(const Prefs &prefs) {
    switch (prefs.language) {
    case Prefs::LanguagePref::DE: return strings::Lang::DE;
    case Prefs::LanguagePref::EN: return strings::Lang::EN;
    case Prefs::LanguagePref::FR: return strings::Lang::FR;
    case Prefs::LanguagePref::IT: return strings::Lang::IT;
    case Prefs::LanguagePref::ES: return strings::Lang::ES;
    default: break;
    }
    strings::Lang result = strings::Lang::EN;
    if (R_SUCCEEDED(setInitialize())) {
        u64 languageCode = 0;
        if (R_SUCCEEDED(setGetSystemLanguage(&languageCode))) {
            SetLanguage setLang;
            if (R_SUCCEEDED(setMakeLanguage(languageCode, &setLang))) {
                switch (setLang) {
                case SetLanguage_DE: result = strings::Lang::DE; break;
                case SetLanguage_FR:
                case SetLanguage_FRCA: result = strings::Lang::FR; break;
                case SetLanguage_IT: result = strings::Lang::IT; break;
                case SetLanguage_ES:
                case SetLanguage_ES419: result = strings::Lang::ES; break;
                default: break;
                }
            }
        }
        setExit();
    }
    return result;
}

void applyLanguage(const Prefs &prefs) {
    strings::setLanguage(resolveLanguage(prefs));
}
