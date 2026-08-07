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
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

/**
 * Top of the "Konsolenspezifische Einstellungen" nav row (SettingsActivity)
 * -- lists the four consoles Unison streams from, each a whole-row nav
 * target into AntialiasingActivity (now the per-console detail screen: a
 * bilinear-filter switch plus a video-mode picker, both keyed by that
 * console's own stream_type). Replaces what used to be two separate
 * top-level SettingsActivity rows (a global "Bilineare Filterung" screen
 * that WAS this list, just with an inline Switch per row instead of a nav
 * target; and a global "Videomodus" row/screen shared by every console) --
 * see AntialiasingActivity's/VideoModeActivity's own comments on why each
 * setting moved from "one value for every console" to "one value per
 * console".
 */
@OptIn(ExperimentalMaterial3Api::class)
class ConsoleSettingsActivity : LocalizedActivity() {

    private data class ConsoleRow(val streamType: String, val labelRes: Int, val videoModeLabelRes: Int)

    private lateinit var prefs: Prefs
    private var consoles = mutableStateListOf<ConsoleRow>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = Prefs(this)
        loadConsoles()

        setContent {
            UnisonTheme {
                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    Scaffold(
                        topBar = {
                            TopAppBar(
                                title = { Text(stringResource(R.string.settings_console_specific)) },
                                navigationIcon = {
                                    TextButton(onClick = { finish() }) { Text(stringResource(R.string.back)) }
                                }
                            )
                        }
                    ) { innerPadding ->
                        LazyColumn(modifier = Modifier.padding(innerPadding).padding(horizontal = 16.dp).fillMaxSize()) {
                            items(consoles.size) { index ->
                                val row = consoles[index]
                                Row(
                                    verticalAlignment = Alignment.CenterVertically,
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .clickable {
                                            startActivity(
                                                Intent(this@ConsoleSettingsActivity, AntialiasingActivity::class.java)
                                                    .putExtra(AntialiasingActivity.EXTRA_STREAM_TYPE, row.streamType)
                                                    .putExtra(AntialiasingActivity.EXTRA_LABEL_RES, row.labelRes)
                                            )
                                        }
                                        .padding(vertical = 12.dp)
                                ) {
                                    Column(modifier = Modifier.weight(1f)) {
                                        Text(stringResource(row.labelRes), style = MaterialTheme.typography.titleMedium)
                                        Text(
                                            stringResource(row.videoModeLabelRes),
                                            style = MaterialTheme.typography.bodySmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant
                                        )
                                    }
                                }
                                HorizontalDivider()
                            }
                        }
                    }
                }
            }
        }
    }

    // Refreshed on return from AntialiasingActivity too (picking a new video
    // mode there and coming back here shouldn't leave this list's own
    // subtitles stale), same reasoning as AntialiasingActivity's own
    // onResume() re-read.
    override fun onResume() {
        super.onResume()
        loadConsoles()
    }

    private fun loadConsoles() {
        val rows = listOf(
            GbaStreamClient.STREAM_TYPE_GC_GBA_LINK to R.string.console_gc_gba_link,
            "WIIU_GAMEPAD" to R.string.console_wiiu_gamepad,
            "N3DS_BOTTOM_SCREEN" to R.string.console_n3ds_bottom_screen,
            "NDS_BOTTOM_SCREEN" to R.string.console_nds_bottom_screen,
        ).map { (streamType, labelRes) ->
            val videoMode = prefs.videoModeFor(streamType)
            val videoModeLabelRes = Prefs.VIDEO_MODES.find { it.value == videoMode }?.labelRes
                ?: R.string.video_mode_tiles
            ConsoleRow(streamType, labelRes, videoModeLabelRes)
        }
        consoles.clear()
        consoles.addAll(rows)
    }
}
