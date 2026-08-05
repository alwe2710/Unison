plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.unison.android"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.unison.android"
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "0.1"
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        compose = true
    }

    // Robolectric needs the app's own resources (strings.xml etc.) on its
    // classpath to resolve stringResource() calls inside a Composable under
    // test -- without this, SettingsActivityTest's real Compose interaction
    // (see its own comment) would only get placeholder/missing-resource
    // text back instead of the real localized strings.
    testOptions {
        unitTests {
            isIncludeAndroidResources = true
        }
    }
}

// Compose + Material 3 (the UI toolkit/design system this app is built with)
// is the one deliberate dependency beyond Kotlin/Android defaults; everything
// that actually matters for the stream itself (transport, protocol, codec)
// still lives entirely in the native unison_core library.
dependencies {
    // Pinned to a BOM/activity-compose pairing that still targets compileSdk
    // 34 (this environment's AGP 8.5.0 caps out there); newer Compose/
    // activity-compose releases require compileSdk 35/36 and AGP 8.6+/8.9+,
    // which would cascade into re-provisioning most of the toolchain.
    val composeBom = platform("androidx.compose:compose-bom:2024.09.00")
    implementation(composeBom)

    implementation("androidx.activity:activity-compose:1.9.2")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")

    // Plain JVM unit tests (src/test/, run via `gradlew testDebugUnitTest`,
    // no emulator/device needed) -- the "generell: Sprach- und Bilinear-
    // Filter-Settings" test category's pure logic (Prefs.defaultBilinearFor(),
    // LocaleHelper.resolveLocaleTag()).
    testImplementation("junit:junit:4.13.2")

    // Real Compose UI interaction under Robolectric (universal-client test
    // category) -- still runs as a plain JVM unit test (testDebugUnitTest,
    // no emulator/device/KVM needed in CI), but drives an actual rendered
    // Compose semantics tree with real click/toggle gestures, unlike the
    // plain-logic tests above. Scoped to SettingsActivity specifically
    // (its on-screen-controls/bilinear-filter toggles) -- PlayerActivity's
    // own overlay is tightly coupled to GbaStreamClient's native (JNI)
    // methods (external fun nativeSendInput() etc., GbaStreamClient.kt),
    // which Robolectric can't exercise here; see the test file's own
    // comment for what that leaves untested and why.
    testImplementation("androidx.compose.ui:ui-test-junit4")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
    testImplementation("org.robolectric:robolectric:4.13")
    testImplementation("androidx.test:core:1.6.1")
    testImplementation("androidx.test.ext:junit:1.2.1")
}
