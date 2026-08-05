package com.unison.android

import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
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
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

/**
 * Per-console antialiasing (bilinear vs. nearest-neighbor upscale), split
 * out of SettingsActivity into its own screen -- same reasoning as
 * KeyBindingsActivity: keeps the main Settings screen from being dominated
 * by a list, this being the second one now. One row per docs/protocol.md
 * stream_type; PlayerActivity reads whatever's configured here in advance
 * via Prefs.bilinearFor(), since it has no reason to open this screen
 * itself (see its EXTRA_STREAM_TYPE / MenuActivity.launchPlayer()).
 */
@OptIn(ExperimentalMaterial3Api::class)
class AntialiasingActivity : LocalizedActivity() {

    private data class ConsoleFilterRow(val streamType: String, val labelRes: Int, val bilinear: Boolean)

    private lateinit var prefs: Prefs
    private var consoleFilters = mutableStateListOf<ConsoleFilterRow>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = Prefs(this)
        consoleFilters.addAll(
            listOf(
                ConsoleFilterRow(
                    GbaStreamClient.STREAM_TYPE_GC_GBA_LINK,
                    R.string.console_gc_gba_link,
                    prefs.bilinearFor(GbaStreamClient.STREAM_TYPE_GC_GBA_LINK)
                ),
                ConsoleFilterRow("WIIU_GAMEPAD", R.string.console_wiiu_gamepad, prefs.bilinearFor("WIIU_GAMEPAD")),
                ConsoleFilterRow(
                    "N3DS_BOTTOM_SCREEN",
                    R.string.console_n3ds_bottom_screen,
                    prefs.bilinearFor("N3DS_BOTTOM_SCREEN")
                ),
                ConsoleFilterRow(
                    "NDS_BOTTOM_SCREEN",
                    R.string.console_nds_bottom_screen,
                    prefs.bilinearFor("NDS_BOTTOM_SCREEN")
                ),
            )
        )

        setContent {
            UnisonTheme {
                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    Scaffold(
                        topBar = {
                            TopAppBar(
                                title = { Text(stringResource(R.string.settings_antialiasing)) },
                                navigationIcon = {
                                    TextButton(onClick = { finish() }) { Text(stringResource(R.string.back)) }
                                }
                            )
                        }
                    ) { innerPadding ->
                        LazyColumn(modifier = Modifier.padding(innerPadding).padding(horizontal = 16.dp).fillMaxSize()) {
                            item {
                                Spacer(Modifier.height(8.dp))
                                Text(
                                    stringResource(R.string.settings_bilinear_filter_header),
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                Spacer(Modifier.height(8.dp))
                            }
                            items(consoleFilters.indices.toList()) { index ->
                                val row = consoleFilters[index]
                                Row(
                                    verticalAlignment = Alignment.CenterVertically,
                                    modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp)
                                ) {
                                    Text(stringResource(row.labelRes), modifier = Modifier.weight(1f))
                                    Switch(
                                        checked = row.bilinear,
                                        onCheckedChange = {
                                            consoleFilters[index] = row.copy(bilinear = it)
                                            prefs.setBilinearFor(row.streamType, it)
                                        }
                                    )
                                }
                                HorizontalDivider()
                            }
                        }
                    }
                }
            }
        }
    }
}
