package com.unison.android

import android.content.Intent
import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
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
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

/**
 * On-screen-controls toggle and navigation entries into KeyBindingsActivity
 * and ConsoleSettingsActivity (their own screens now -- see those classes
 * for why they were split out of here). This screen no longer touches key
 * bindings or per-console filter/video-mode state directly at all -- used
 * to have its own "Bilineare Filterung" and "Videomodus" rows, each a
 * single value shared by every console; both moved into
 * ConsoleSettingsActivity's per-console detail screens instead (see that
 * class's own comment), so this top level only needs the one nav row down
 * to that list now.
 *
 * No fixed orientation (see AndroidManifest.xml): this is a form, so it
 * should follow however the device is actually held.
 */
@OptIn(ExperimentalMaterial3Api::class)
class SettingsActivity : LocalizedActivity() {

    private lateinit var prefs: Prefs
    private var onScreenControlsEnabled by mutableStateOf(true)
    private var hardwareDecodeEnabled by mutableStateOf(true)
    private var showDebugOverlay by mutableStateOf(false)
    private var language by mutableStateOf(Prefs.LANGUAGE_SYSTEM)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = Prefs(this)
        onScreenControlsEnabled = prefs.onScreenControlsEnabled
        hardwareDecodeEnabled = prefs.hardwareDecodeEnabled
        showDebugOverlay = prefs.showDebugOverlay
        language = prefs.language

        setContent {
            UnisonTheme {
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
                        Column(
                            modifier = Modifier
                                .padding(innerPadding)
                                .padding(horizontal = 16.dp)
                                .fillMaxSize()
                                .verticalScroll(rememberScrollState())
                        ) {
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
                                    },
                                    modifier = Modifier.testTag("onScreenControlsSwitch")
                                )
                            }

                            HorizontalDivider()

                            // General (not per-console) decoder override --
                            // see Prefs.hardwareDecodeEnabled's own comment.
                            // Subtitle hint shown the same way as the
                            // language row's current-value subtitle below,
                            // but static text rather than a computed value.
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                modifier = Modifier.fillMaxWidth().padding(vertical = 12.dp)
                            ) {
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(stringResource(R.string.settings_hardware_decode))
                                    Text(
                                        stringResource(R.string.settings_hardware_decode_hint),
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                                Switch(
                                    checked = hardwareDecodeEnabled,
                                    onCheckedChange = {
                                        hardwareDecodeEnabled = it
                                        prefs.hardwareDecodeEnabled = it
                                    },
                                    modifier = Modifier.testTag("hardwareDecodeSwitch")
                                )
                            }

                            HorizontalDivider()

                            // Opt-in stats readout drawn over PlayerScreen
                            // (decode latency + cumulative dropped frames,
                            // GbaStreamClient.getStreamStats()) -- off by
                            // default, same "not for everyday use" treatment
                            // as the hardware-decoder toggle above.
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                modifier = Modifier.fillMaxWidth().padding(vertical = 12.dp)
                            ) {
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(stringResource(R.string.settings_debug_overlay))
                                    Text(
                                        stringResource(R.string.settings_debug_overlay_hint),
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                                Switch(
                                    checked = showDebugOverlay,
                                    onCheckedChange = {
                                        showDebugOverlay = it
                                        prefs.showDebugOverlay = it
                                    },
                                    modifier = Modifier.testTag("debugOverlaySwitch")
                                )
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

                            // Own sub-screen (ConsoleSettingsActivity) --
                            // whole-row tap target (system-settings-list-item
                            // style), not a separate "open" button off to the
                            // side -- the row's own click, not just an inner
                            // element's, is what navigates. Same treatment
                            // for every other sub-screen row here.
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable {
                                        startActivity(Intent(this@SettingsActivity, ConsoleSettingsActivity::class.java))
                                    }
                                    .padding(vertical = 12.dp)
                            ) {
                                Text(
                                    stringResource(R.string.settings_console_specific),
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
    }
}
