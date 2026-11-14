package com.unison.android

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
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
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.unit.dp

/**
 * Static "about" screen -- app name, build version, license, repo link.
 * No Prefs/state at all (unlike LanguageActivity, this screen's own
 * template), so there's nothing to persist or refresh on return.
 *
 * BuildConfig.VERSION_NAME is `git describe --tags --always --dirty`'s
 * output at build time (see build.gradle.kts's own comment on
 * versionName) -- not a manually-bumped number, so this screen never goes
 * stale relative to what was actually built.
 */
@OptIn(ExperimentalMaterial3Api::class)
class InfoActivity : LocalizedActivity() {

    companion object {
        const val REPO_URL = "https://github.com/alwe2710/Unison"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            UnisonTheme {
                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    Scaffold(
                        topBar = {
                            TopAppBar(
                                title = { Text(stringResource(R.string.settings_info)) },
                                navigationIcon = {
                                    TextButton(onClick = { finish() }) { Text(stringResource(R.string.back)) }
                                }
                            )
                        }
                    ) { innerPadding ->
                        Column(modifier = Modifier.padding(innerPadding).padding(16.dp).fillMaxWidth()) {
                            Text(stringResource(R.string.app_name), style = MaterialTheme.typography.headlineSmall)
                            Text(
                                stringResource(R.string.info_version, BuildConfig.VERSION_NAME),
                                modifier = Modifier.padding(top = 12.dp)
                            )
                            Text(
                                stringResource(R.string.info_license, "PolyForm Noncommercial 1.0.0"),
                                modifier = Modifier.padding(top = 6.dp)
                            )
                            Text(
                                stringResource(R.string.info_repo),
                                style = MaterialTheme.typography.bodyLarge.copy(textDecoration = TextDecoration.Underline),
                                color = MaterialTheme.colorScheme.primary,
                                modifier = Modifier
                                    .padding(top = 18.dp)
                                    .clickable {
                                        startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(REPO_URL)))
                                    }
                            )
                        }
                    }
                }
            }
        }
    }
}
