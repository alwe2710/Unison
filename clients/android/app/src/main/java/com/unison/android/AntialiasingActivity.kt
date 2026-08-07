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
 * Per-console detail screen, reached from ConsoleSettingsActivity's 4-console
 * list (EXTRA_STREAM_TYPE always required now, unlike this class's own
 * earlier shape where it WAS that list, with an inline Switch per console --
 * moved out to ConsoleSettingsActivity so this screen could show more than a
 * single toggle per console without cramming both the antialiasing switch
 * and a video-mode picker into one flat list row). Two settings live here,
 * both keyed by this one stream_type: bilinear-vs-nearest upscale (inline
 * Switch, same as before) and the video-mode/compression picker (own
 * sub-screen, VideoModeActivity) -- used to be a single global choice shared
 * by every console (SettingsActivity's own former "Videomodus" row), moved
 * here per console for the same reason antialiasing already was.
 */
@OptIn(ExperimentalMaterial3Api::class)
class AntialiasingActivity : LocalizedActivity() {

    companion object {
        const val EXTRA_STREAM_TYPE = "stream_type"
        const val EXTRA_LABEL_RES = "label_res"
    }

    private lateinit var prefs: Prefs
    private lateinit var streamType: String
    private var bilinear by mutableStateOf(false)
    private var videoMode by mutableStateOf(Prefs.VIDEO_MODE_DEFAULT)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = Prefs(this)
        streamType = intent.getStringExtra(EXTRA_STREAM_TYPE) ?: ""
        val labelRes = intent.getIntExtra(EXTRA_LABEL_RES, R.string.settings_console_specific)
        bilinear = prefs.bilinearFor(streamType)
        videoMode = prefs.videoModeFor(streamType)

        setContent {
            UnisonTheme {
                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    Scaffold(
                        topBar = {
                            TopAppBar(
                                title = { Text(stringResource(labelRes)) },
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
                                    stringResource(R.string.settings_antialiasing),
                                    modifier = Modifier.weight(1f)
                                )
                                Switch(
                                    checked = bilinear,
                                    onCheckedChange = {
                                        bilinear = it
                                        prefs.setBilinearFor(streamType, it)
                                    }
                                )
                            }

                            HorizontalDivider()

                            // Own sub-screen (VideoModeActivity), same
                            // whole-row-navigates treatment SettingsActivity's
                            // language/console rows already use -- current
                            // value shown as a subtitle, same idea as a
                            // system settings list item.
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable {
                                        startActivity(
                                            Intent(this@AntialiasingActivity, VideoModeActivity::class.java)
                                                .putExtra(VideoModeActivity.EXTRA_STREAM_TYPE, streamType)
                                        )
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
                        }
                    }
                }
            }
        }
    }

    // Picking a video mode in VideoModeActivity and returning here (finish())
    // wouldn't otherwise refresh this screen's own subtitle -- same
    // language-row staleness fix SettingsActivity.onResume() already needed.
    override fun onResume() {
        super.onResume()
        bilinear = prefs.bilinearFor(streamType)
        videoMode = prefs.videoModeFor(streamType)
    }
}
