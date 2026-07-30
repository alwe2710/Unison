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
 * Same "own sub-screen, tap a row, pick and return" shape as
 * LanguageActivity -- split out here rather than kept as SettingsActivity's
 * own inline Switch, since Prefs.VIDEO_MODES is meant to grow (more codecs
 * later, see docs/protocol.md's hello_ack.video_mode), and a picker list
 * scales to that the way a boolean toggle wouldn't have.
 *
 * Unlike LanguageActivity, this does NOT sort Prefs.VIDEO_MODES by label --
 * these aren't endonyms, and the declared order (recommended default first)
 * is deliberate.
 */
@OptIn(ExperimentalMaterial3Api::class)
class VideoModeActivity : LocalizedActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = Prefs(this)

        setContent {
            FinlinkTheme {
                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    Scaffold(
                        topBar = {
                            TopAppBar(
                                title = { Text(stringResource(R.string.settings_video_mode)) },
                                navigationIcon = {
                                    TextButton(onClick = { finish() }) { Text(stringResource(R.string.back)) }
                                }
                            )
                        }
                    ) { innerPadding ->
                        Column(modifier = Modifier.padding(innerPadding).fillMaxSize()) {
                            for (option in Prefs.VIDEO_MODES) {
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .clickable {
                                            prefs.videoMode = option.value
                                            finish()
                                        }
                                        .padding(horizontal = 16.dp, vertical = 14.dp)
                                ) {
                                    Text(stringResource(option.labelRes))
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
