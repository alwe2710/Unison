package com.finlink.android

import android.content.Intent
import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
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
import androidx.compose.ui.text.font.FontWeight
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

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = Prefs(this)
        onScreenControlsEnabled = prefs.onScreenControlsEnabled
        language = prefs.language

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
                        Column(modifier = Modifier.padding(innerPadding).padding(16.dp).fillMaxSize()) {
                            Row(verticalAlignment = Alignment.CenterVertically) {
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

                            Spacer(Modifier.height(16.dp))
                            HorizontalDivider()
                            Spacer(Modifier.height(16.dp))

                            // Three-way pick (not just "German"/"English"):
                            // "System" lets the user get back to following
                            // the device locale after having overridden it.
                            // LocalizedActivity.onResume() recreates this
                            // (and every other open Activity) once the
                            // resolved language actually changes, so the
                            // whole UI re-renders in the new language
                            // immediately.
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text(
                                    stringResource(R.string.settings_language),
                                    modifier = Modifier.weight(1f)
                                )
                                for ((value, labelRes) in listOf(
                                    Prefs.LANGUAGE_SYSTEM to R.string.language_system,
                                    "de" to R.string.language_german,
                                    "en" to R.string.language_english
                                )) {
                                    TextButton(onClick = {
                                        language = value
                                        prefs.language = value
                                    }) {
                                        Text(
                                            stringResource(labelRes),
                                            fontWeight = if (language == value) FontWeight.Bold else FontWeight.Normal
                                        )
                                    }
                                }
                            }

                            Spacer(Modifier.height(16.dp))
                            HorizontalDivider()
                            Spacer(Modifier.height(16.dp))

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
}
