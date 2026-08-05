package com.unison.android

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext

// Matches assets/logo/unison-logo.png: dark navy background, cyan glow.
private val UnisonCyan = Color(0xFF4DD8E8)
private val UnisonCyanMuted = Color(0xFF1E8FA6)
private val UnisonNavy = Color(0xFF0A1128)
private val UnisonNavyLight = Color(0xFF16213E)

private val UnisonDarkScheme = darkColorScheme(
    primary = UnisonCyan,
    onPrimary = Color.Black,
    secondary = UnisonCyanMuted,
    background = UnisonNavy,
    surface = UnisonNavyLight,
)

private val UnisonLightScheme = lightColorScheme(
    primary = UnisonCyanMuted,
    secondary = UnisonCyan,
)

/**
 * Material 3 theme, with dynamic color (Android 12+) preferred when
 * available and a fixed cyan-on-navy scheme derived from the app logo as
 * the fallback everywhere else.
 */
@Composable
fun UnisonTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    dynamicColor: Boolean = true,
    content: @Composable () -> Unit
) {
    val context = LocalContext.current
    val colorScheme = when {
        dynamicColor && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ->
            if (darkTheme) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        darkTheme -> UnisonDarkScheme
        else -> UnisonLightScheme
    }

    MaterialTheme(
        colorScheme = colorScheme,
        content = content
    )
}
