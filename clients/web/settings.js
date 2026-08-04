// Pure logic for the web client's language + bilinear-filter settings --
// pulled out of index.html's inline <script> into its own file so it has
// one place to unit-test under Node (see settings_test.mjs) instead of
// only being exercisable in a real browser. Loaded the same way as
// strings_generated.js/finlink_core.js (plain <script src="settings.js">,
// classic script, not a module -- index.html's own inline script stays
// classic too, so nothing about its execution timing changes), with a
// module.exports guard at the bottom so the exact same file also works as
// a CommonJS module under Node for the test.
//
// Deliberately excludes anything touching localStorage/navigator directly
// (langPref, filterPrefs, setLangPref, setBilinearFor stay in index.html)
// -- these functions only take/return plain values, same reasoning as the
// C/C++ clients' audio_ring/audio_remix/screen_choice extractions this
// session.

// SUPPORTED_LANGS: keep in sync with i18n/strings.json's language set (and
// index.html's own copy of this same list).
const SUPPORTED_LANGS = ['de', 'en', 'fr', 'it', 'es'];

// langPref: 'system' or an explicit override. systemLang: the browser's
// own language tag (navigator.language), already lowercased+truncated to
// its first 2 chars by the caller. Falls back to 'en' when the resolved
// language isn't one of SUPPORTED_LANGS -- same "English if undetermined/
// unsupported" policy as every other client.
function resolvedLang(langPref, systemLang) {
  if (SUPPORTED_LANGS.includes(langPref)) return langPref;
  return SUPPORTED_LANGS.includes(systemLang) ? systemLang : 'en';
}

// {0}/{1}/... substitution, the same convention every client's string
// table uses (see docs, and Android/Switch/3DS/NDS's own equivalents).
function formatString(text, args) {
  return text.replace(/\{(\d+)\}/g, (_, i) => args[i]);
}

// GBA/DS pixel art (GC_GBA_LINK) reads best at native resolution (nearest-
// neighbor); the other three stream types are already-upscaled/higher-
// effective-resolution renders that read better smoothed (bilinear).
function defaultBilinearFor(streamType) {
  return streamType === 'WIIU_GAMEPAD' || streamType === 'N3DS_BOTTOM_SCREEN' || streamType === 'NDS_BOTTOM_SCREEN';
}

// filterPrefs: the parsed finlinkWebBilinearByStreamType localStorage
// object (streamType -> bool), passed in rather than read from
// localStorage directly so this stays a pure function of its arguments.
function bilinearFor(streamType, filterPrefs) {
  return streamType in filterPrefs ? !!filterPrefs[streamType] : defaultBilinearFor(streamType);
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = { SUPPORTED_LANGS, resolvedLang, formatString, defaultBilinearFor, bilinearFor };
}
