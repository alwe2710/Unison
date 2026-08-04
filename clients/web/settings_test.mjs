// Unit tests for settings.js's pure language/bilinear-filter logic --
// same "generell: Sprach- und Bilinear-Filter-Settings" test category as
// the Android/3DS/NDS/Switch clients' own settings tests. Run under plain
// Node (no browser, no wasm module needed here -- unlike bridge_test.mjs),
// same check()-helper convention as that file.

import { SUPPORTED_LANGS, resolvedLang, formatString, defaultBilinearFor, bilinearFor } from "./settings.js";

let failures = 0;
function check(name, cond) {
  if (cond) {
    console.log("OK  ", name);
  } else {
    console.error("FAIL", name);
    failures++;
  }
}

// ---------------- resolvedLang ----------------

for (const lang of SUPPORTED_LANGS) {
  check(`resolvedLang: explicit pref '${lang}' wins over system`, resolvedLang(lang, "en") === lang);
}
check("resolvedLang: 'system' pref resolves to a supported system language", resolvedLang("system", "de") === "de");
check(
  "resolvedLang: 'system' pref falls back to 'en' for an unsupported system language",
  resolvedLang("system", "ja") === "en"
);
check(
  "resolvedLang: an explicit pref that isn't actually supported (stale localStorage value) falls back through to system",
  resolvedLang("xx", "fr") === "fr"
);
check(
  "resolvedLang: empty system language (navigator.language unavailable) falls back to 'en'",
  resolvedLang("system", "") === "en"
);

// ---------------- formatString ----------------

check("formatString: single placeholder", formatString("Fehler: {0}", ["timeout"]) === "Fehler: timeout");
check(
  "formatString: multiple placeholders, out of order in the template",
  formatString("{1} then {0}", ["second", "first"]) === "first then second"
);
check("formatString: no placeholders passes text through unchanged", formatString("Verbunden", []) === "Verbunden");
check(
  "formatString: repeated placeholder substitutes the same arg both times",
  formatString("{0}-{0}", ["x"]) === "x-x"
);

// ---------------- defaultBilinearFor ----------------

check("defaultBilinearFor: GC_GBA_LINK defaults to nearest (false)", defaultBilinearFor("GC_GBA_LINK") === false);
for (const streamType of ["WIIU_GAMEPAD", "N3DS_BOTTOM_SCREEN", "NDS_BOTTOM_SCREEN"]) {
  check(`defaultBilinearFor: ${streamType} defaults to bilinear (true)`, defaultBilinearFor(streamType) === true);
}
check(
  "defaultBilinearFor: an unrecognized stream_type defaults to nearest (false), not bilinear",
  defaultBilinearFor("SOME_FUTURE_STREAM_TYPE") === false
);

// ---------------- bilinearFor ----------------

check(
  "bilinearFor: no stored pref falls back to defaultBilinearFor (GC_GBA_LINK, nearest)",
  bilinearFor("GC_GBA_LINK", {}) === false
);
check(
  "bilinearFor: no stored pref falls back to defaultBilinearFor (WIIU_GAMEPAD, bilinear)",
  bilinearFor("WIIU_GAMEPAD", {}) === true
);
check(
  "bilinearFor: an explicit stored 'false' overrides a default of true",
  bilinearFor("WIIU_GAMEPAD", { WIIU_GAMEPAD: false }) === false
);
check(
  "bilinearFor: an explicit stored 'true' overrides a default of false",
  bilinearFor("GC_GBA_LINK", { GC_GBA_LINK: true }) === true
);
check(
  "bilinearFor: a stored pref for a different stream_type doesn't affect this one",
  bilinearFor("GC_GBA_LINK", { WIIU_GAMEPAD: true }) === false
);

if (failures > 0) {
  console.error(`${failures} test(s) failed`);
  process.exit(1);
}
console.log("settings: all tests passed");
