package com.finlink.android

import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

/**
 * Split out of SettingsActivity (same reasoning as AntialiasingActivity/
 * KeyBindingsActivity) into its own selection-list screen. Plain rows, no
 * radio buttons/checkmarks -- tapping a row sets the preference and
 * immediately returns to SettingsActivity (finish()), same one-shot
 * "pick and you're done" flow as picking a value from a dropdown, rather
 * than staying on this screen for further review.
 *
 * SettingsActivity's own onResume() re-reads Prefs.language directly (see
 * its own comment for why LocalizedActivity's locale-mismatch recreate()
 * alone doesn't always catch it), so its "Sprache"/"Language" row's
 * subtitle is correct by the time this finish() reveals it again.
 */
@OptIn(ExperimentalMaterial3Api::class)
class LanguageActivity : LocalizedActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = Prefs(this)

        setContent {
            FinlinkTheme {
                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    Scaffold(
                        topBar = {
                            TopAppBar(
                                title = { Text(stringResource(R.string.settings_language)) },
                                navigationIcon = {
                                    TextButton(onClick = { finish() }) { Text(stringResource(R.string.back)) }
                                }
                            )
                        }
                    ) { innerPadding ->
                        // Sorted by the displayed label, not a fixed order --
                        // "System" is localized like any other UI string,
                        // but "Deutsch"/"English" are fixed endonyms (see
                        // strings.json), so this only actually reorders
                        // relative to "System"/"Système"/... as more
                        // languages are added later.
                        val options = listOf(
                            Prefs.LANGUAGE_SYSTEM to stringResource(R.string.language_system),
                            "de" to stringResource(R.string.language_german),
                            "en" to stringResource(R.string.language_english)
                        ).sortedBy { it.second }
                        Column(modifier = Modifier.padding(innerPadding).fillMaxSize()) {
                            for ((value, label) in options) {
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .clickable {
                                            prefs.language = value
                                            finish()
                                        }
                                        .padding(horizontal = 16.dp, vertical = 14.dp)
                                ) {
                                    Text(label)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
