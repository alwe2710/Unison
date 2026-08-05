#include "prefs.hpp"

#include <fstream>
#include <sys/stat.h>

#include <switch.h>

#include "settings_pref.hpp"

namespace {
constexpr const char *kDir = "sdmc:/switch/unison";
constexpr const char *kFile = "sdmc:/switch/unison/settings.cfg";

// "bilinear_video_filter.GC_GBA_LINK" -- the prefix every per-stream-type
// key uses within `values` (round-tripped wholesale, see save() below), so
// a server introducing a new stream_type doesn't need a client code change
// just to remember its filter preference.
constexpr const char *kBilinearKeyPrefix = "bilinear_video_filter.";
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
// other client. The actual pref/system-language resolution decision lives
// in resolveLanguagePref() (settings_pref.hpp/.cpp) -- this just does the
// setGetSystemLanguage()/setMakeLanguage() calls and translates the result
// into std::optional<strings::Lang>.
strings::Lang resolveLanguage(const Prefs &prefs) {
    std::optional<strings::Lang> systemLanguage;
    if (R_SUCCEEDED(setInitialize())) {
        u64 languageCode = 0;
        if (R_SUCCEEDED(setGetSystemLanguage(&languageCode))) {
            SetLanguage setLang;
            if (R_SUCCEEDED(setMakeLanguage(languageCode, &setLang))) {
                switch (setLang) {
                case SetLanguage_DE: systemLanguage = strings::Lang::DE; break;
                case SetLanguage_FR:
                case SetLanguage_FRCA: systemLanguage = strings::Lang::FR; break;
                case SetLanguage_IT: systemLanguage = strings::Lang::IT; break;
                case SetLanguage_ES:
                case SetLanguage_ES419: systemLanguage = strings::Lang::ES; break;
                default: break;
                }
            }
        }
        setExit();
    }
    return resolveLanguagePref(prefs.language, systemLanguage);
}

void applyLanguage(const Prefs &prefs) {
    strings::setLanguage(resolveLanguage(prefs));
}
