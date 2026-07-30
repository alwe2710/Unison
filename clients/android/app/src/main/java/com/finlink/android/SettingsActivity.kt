package com.finlink.android

import android.content.Intent
import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

/**
 * On-screen-controls toggle and navigation entries into KeyBindingsActivity
 * and AntialiasingActivity (their own screens now -- see those classes for
 * why they were split out of here). This screen no longer touches key
 * bindings or per-console filter state directly at all.
 *
 * No fixed orientation (see AndroidManifest.xml): this is a form, so it
 * should follow however the device is actually held.
 */
@OptIn(ExperimentalMaterial3Api::class)
class SettingsActivity : LocalizedActivity() {

    private lateinit var prefs: Prefs
    private var onScreenControlsEnabled by mutableStateOf(true)
    private var language by mutableStateOf(Prefs.LANGUAGE_SYSTEM)
    private var videoMode by mutableStateOf(Prefs.VIDEO_MODE_DEFAULT)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = Prefs(this)
        onScreenControlsEnabled = prefs.onScreenControlsEnabled
        language = prefs.language
        videoMode = prefs.videoMode

        setContent {
            FinlinkTheme {
                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    Scaffold(
                        topBar = {
                            TopAppBar(
                                title = { Text(stringResource(R.string.settings)) },
                                navigationIcon = {
                                    TextButton(onClick = { finish() }) { Text(stringResource(R.string.back)) }
                                }
                            )
                        }
                    ) { innerPadding ->
                        Column(modifier = Modifier.padding(innerPadding).padding(horizontal = 16.dp).fillMaxSize()) {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                modifier = Modifier.fillMaxWidth().padding(vertical = 12.dp)
                            ) {
                                Text(
                                    stringResource(R.string.settings_on_screen_controls),
                                    modifier = Modifier.weight(1f)
                                )
                                Switch(
                                    checked = onScreenControlsEnabled,
                                    onCheckedChange = {
                                        onScreenControlsEnabled = it
                                        prefs.onScreenControlsEnabled = it
                                    }
                                )
                            }

                            HorizontalDivider()

                            // Own sub-screen (VideoModeActivity), same
                            // whole-row-navigates treatment as the language
                            // row below -- sent to the server as
                            // hello_ack.video_mode at the next connection's
                            // handshake (see docs/protocol.md). Only
                            // WIIU_GAMEPAD (Cemu) honors it today; servers
                            // that don't recognize the field just ignore it.
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable {
                                        startActivity(Intent(this@SettingsActivity, VideoModeActivity::class.java))
                                    }
                                    .padding(vertical = 12.dp)
                            ) {
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(stringResource(R.string.settings_video_mode), style = MaterialTheme.typography.titleMedium)
                                    Text(
                                        stringResource(
                                            Prefs.VIDEO_MODES.find { it.value == videoMode }?.labelRes
                                                ?: R.string.video_mode_tiles
                                        ),
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }

                            HorizontalDivider()

                            // Own sub-screen (LanguageActivity), same
                            // whole-row-navigates treatment as the two rows
                            // below -- a selection list (like the device's
                            // own system-settings language picker) instead
                            // of three inline buttons, since this is one
                            // exclusive choice, not three independent
                            // toggles. Its current value shown as a
                            // subtitle here, same idea as a system settings
                            // list item.
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable {
                                        startActivity(Intent(this@SettingsActivity, LanguageActivity::class.java))
                                    }
                                    .padding(vertical = 12.dp)
                            ) {
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(stringResource(R.string.settings_language), style = MaterialTheme.typography.titleMedium)
                                    Text(
                                        stringResource(
                                            Prefs.LANGUAGES.find { it.value == language }?.labelRes
                                                ?: R.string.language_system
                                        ),
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }

                            HorizontalDivider()

                            // Whole-row tap target (system-settings-list-item
                            // style), not a separate "open" button off to the
                            // side -- the row's own click, not just an inner
                            // element's, is what navigates. Same treatment
                            // for both sub-screens.
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable {
                                        startActivity(Intent(this@SettingsActivity, AntialiasingActivity::class.java))
                                    }
                                    .padding(vertical = 12.dp)
                            ) {
                                Text(
                                    stringResource(R.string.settings_antialiasing),
                                    style = MaterialTheme.typography.titleMedium,
                                    modifier = Modifier.weight(1f)
                                )
                            }

                            HorizontalDivider()

                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable {
                                        startActivity(Intent(this@SettingsActivity, KeyBindingsActivity::class.java))
                                    }
                                    .padding(vertical = 12.dp)
                            ) {
                                Text(
                                    stringResource(R.string.settings_key_bindings),
                                    style = MaterialTheme.typography.titleMedium,
                                    modifier = Modifier.weight(1f)
                                )
                            }
                        }
                    }
                }
            }
        }
    }

    // LocalizedActivity.onResume() only recreate()s this Activity when the
    // *resolved display language* actually changed -- but picking "System"
    // while the device locale already happens to match the language that
    // was explicitly selected before (e.g. both are English) resolves to
    // the same language, so that check alone misses it, leaving this
    // screen's own `language` state (and therefore this row's subtitle)
    // stale even though the underlying preference did change. Re-reading
    // it here directly covers that case too, on top of (not instead of)
    // the superclass's own locale-mismatch recreate().
    override fun onResume() {
        super.onResume()
        language = prefs.language
        videoMode = prefs.videoMode
    }
}
