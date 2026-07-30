# clients/android

Three-page demo app: **Menu** (host entry + network discovery + link to
Settings), **Settings** (on-screen-controls toggle + physical key bindings),
**Player** (the actual stream: fullscreen video, audio, input). UI is
[Jetpack Compose](https://developer.android.com/compose) with
[Material 3](https://m3.material.io/) — see [Design](#design) below for why
and what that cost in this environment.

## Architecture

All protocol/transport logic runs in native C (`app/src/main/cpp/`), not
Kotlin — the Activities are a pure UI/orchestration layer on top:

- **`core/`** (via `add_subdirectory`, see `cpp/CMakeLists.txt`) — WS
  handshake/framing, app protocol, deflate. Unmodified from core, no
  Android-specific changes needed.
- **`cpp/jni_bridge.c`** — everything Android-specific: the raw POSIX
  socket (connect/send/recv), a background thread for the session loop
  (mirrors the server-side poll-loop pattern in `GBAStreamHost.cpp`: a short
  `poll()` timeout, then check whether any input is queued to send), and the
  JNI callbacks back into Kotlin.
- **`GbaStreamClient.kt`** — thin wrapper around the three native methods;
  all the real work happens in `jni_bridge.c`.
- **`MenuActivity.kt`** — host entry + P1–P4 picker (`GET /status` on
  6801–6804, plain `HttpURLConnection`, deliberately *not* through
  `finlink_core`/the WebSocket path, since it's not part of the stream
  protocol), plus network discovery (subnet sweep against port 6800 — no
  finlink server advertises itself this way, no mDNS/UPnP, see
  [`docs/protocol.md`](../../docs/protocol.md)). The lobby (6800) has no
  bundled status for this; each player port has to be queried individually.
- **`SettingsActivity.kt`** — on-screen-controls toggle, key bindings
  (physical key/controller button per GBA button, via `dispatchKeyEvent`
  interception), and the language picker (see [Localization](#localization)
  below) — all persisted in `Prefs.kt` (`SharedPreferences`).
- **`LanguageActivity.kt`** / **`LocaleHelper.kt`** / **`LocalizedActivity.kt`**
  — the language picker's own screen, Configuration-wrapping helper, and
  shared Activity base class (`attachBaseContext` override), see
  [Localization](#localization) below.
- **`PlayerActivity.kt`** — connects immediately on start (host/port arrive
  as Intent extras from `MenuActivity`), renders video as `Bitmap.RGB_565`
  (matches the wire format exactly) via `Image(contentScale = ContentScale.Fit)`
  (scaled to fullscreen, never stretched), plays audio via `AudioTrack`,
  combines touch and physical key input (separate bitmasks, OR'd together
  when sending — same as the original web client, which mixes
  keyboard/touch/gamepad the same way).

Random bytes for the WS key and frame masking come from `arc4random_buf`
(Bionic libc standard, no extra dependency).

Menu and Settings have **no** orientation lock (follow device rotation),
Player stays landscape-locked (fixed GBA aspect ratio requirement) — see
`AndroidManifest.xml`.

## Localization

`SettingsActivity`'s "Sprache"/"Language" row opens `LanguageActivity`, a
plain alphabetically-sorted list (System, plus every language in
[`i18n/strings.json`](../../i18n/strings.json)) — tapping an entry sets
`Prefs.language` and returns immediately. `LocaleHelper.wrap()` applies the
resolved `Locale` to a `Context`'s `Configuration`; every Activity extends
`LocalizedActivity`, which overrides `attachBaseContext` with that wrapper
and `recreate()`s itself in `onResume()` if the resolved language changed
since it was created (there's no `androidx.appcompat` dependency here, see
[Design](#design) below, so no `AppCompatDelegate.setApplicationLocales()`
to lean on instead).

## Design

"Modern Material Design" meant: the UI layer moved entirely from XML views
to Jetpack Compose + Material 3 (`Theme.kt`), with a dynamic color scheme
(Material You, Android 12+) and a static cyan-on-navy fallback matching
[`assets/logo/`](../../assets/logo/) for older devices.

Dependency versions are deliberately *not* the latest: `compose-bom
2024.09.00` + `activity-compose 1.9.2` instead of the newest releases,
because newer versions require `compileSdk 35/36` and AGP 8.6+/8.9+ — that
would have triggered a cascade of further toolchain upgrades in this
environment. Functionally full-featured Material 3, just a year or two
behind the cutting edge.

## Building

This environment originally had neither the Android NDK/build tools, JDK 17,
nor Gradle (only `platform-tools`/`adb`, JDK 8). For a full build, JDK 17
(Temurin), the Android cmdline-tools, `platform-tools`,
`platforms;android-34`, `build-tools;34.0.0`, and Gradle 8.7 were installed
locally (AGP also pulled in its own NDK version on the first build, since no
`ndkVersion` is pinned in `app/build.gradle.kts`).

```sh
cd clients/android
./gradlew assembleDebug
# APK liegt danach unter app/build/outputs/apk/debug/app-debug.apk
```

Prerequisite: Android SDK with `platforms;android-34` and
`build-tools;34.0.0`, plus an `sdk.dir` in `clients/android/local.properties`
(or `ANDROID_HOME`/`ANDROID_SDK_ROOT` set) — Android Studio sets this up
automatically when opening the project.

## Trying it out

1. Start any finlink server — e.g. Dolphin ([dolphin-gba-stream](https://github.com/)
   fork) with a GC port set to "GBA (Client Stream)", or the Cemu/Azahar/
   melonDS finlink forks (see the root [`README.md`](../../README.md#context)
   for all four).
2. In the app, enter the host IP (e.g. `192.168.1.5`) and "Connect" — or
   "Search for servers" for automatic network discovery.
3. If the server has multiple slots (currently only Dolphin's
   `GC_GBA_LINK`), tap a free P slot.

**Verified on real hardware** (Samsung Galaxy S22, over Wi-Fi `adb`): the
Menu, Settings, and Player screens run cleanly in Compose/Material 3,
including the dynamic color scheme and correct rotation behavior (Menu/
Settings follow device orientation, Player stays landscape). A real
connection to a running Dolphin stream was also tested successfully — video
and audio playback both work in Player. Not separately tested against the
other three forks, though they speak the same protocol.

## Known gaps (deliberately out of scope for this demo)

- The touch overlay is a single row of buttons, not a D-pad layout.
- No reconnect logic on a dropped connection.
- Lobby search queries the four ports sequentially, not in parallel (slower
  accordingly on timeouts); discovery search, by contrast, already runs in
  parallel (thread pool).
