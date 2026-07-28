package com.finlink.android

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
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
import androidx.compose.ui.unit.dp

/**
 * On-screen-controls toggle, display options, and a navigation entry into
 * KeyBindingsActivity (its own screen now -- see that class for why it was
 * split out of here). This screen no longer touches key bindings directly
 * at all, so it no longer needs dispatchKeyEvent interception either.
 *
 * No fixed orientation (see AndroidManifest.xml): this is a form, so it
 * should follow however the device is actually held.
 */
@OptIn(ExperimentalMaterial3Api::class)
class SettingsActivity : ComponentActivity() {

    private lateinit var prefs: Prefs
    private var onScreenControlsEnabled by mutableStateOf(true)
    private var bilinearVideoFilter by mutableStateOf(false)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = Prefs(this)
        onScreenControlsEnabled = prefs.onScreenControlsEnabled
        bilinearVideoFilter = prefs.bilinearVideoFilter

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

                            Text(stringResource(R.string.settings_display), style = MaterialTheme.typography.titleMedium)
                            Spacer(Modifier.height(8.dp))
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text(
                                    stringResource(R.string.settings_bilinear_filter),
                                    modifier = Modifier.weight(1f)
                                )
                                Switch(
                                    checked = bilinearVideoFilter,
                                    onCheckedChange = {
                                        bilinearVideoFilter = it
                                        prefs.bilinearVideoFilter = it
                                    }
                                )
                            }

                            Spacer(Modifier.height(16.dp))
                            HorizontalDivider()
                            Spacer(Modifier.height(16.dp))

                            // Whole-row tap target (system-settings-list-item
                            // style), not a separate "open" button off to the
                            // side -- the row's own click, not just an inner
                            // element's, is what navigates.
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
