package com.finlink.android

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.text.Editable
import android.text.InputFilter
import android.text.TextWatcher
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView

/**
 * Own full Activity for GbaStreamClient.Listener.onTextInputRequest() --
 * launched from PlayerActivity (registerForActivityResult), not shown as a
 * Dialog on top of it. The server's own on-screen keyboard (the emulator's,
 * e.g. Cemu's WiiU software keyboard) never reaches this client as pixels
 * the video stream shows, so this stands in for it entirely.
 *
 * A real EditText (AndroidView, not Compose's own TextField, which always
 * suppresses the system keyboard's native fullscreen "extract mode") gets
 * the system keyboard to take over the whole screen itself. Doing that from
 * a genuine Activity rather than a Dialog matters just as much as the
 * EditText choice: a Dialog is a second window stacked on PlayerActivity's
 * own, and resizing/showing that second window alongside the IME's own
 * show/hide animation is what read as the popup "jumping" -- a plain
 * Activity resizes for the IME the same unremarkable way every other
 * Android text-entry screen does.
 */
@OptIn(ExperimentalMaterial3Api::class)
class TextInputActivity : LocalizedActivity() {

    companion object {
        const val EXTRA_MAX_LENGTH = "max_length"
        const val EXTRA_INITIAL_TEXT = "initial_text"
        const val EXTRA_RESULT_TEXT = "result_text"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val maxLength = intent.getIntExtra(EXTRA_MAX_LENGTH, 0)
        val initialText = intent.getStringExtra(EXTRA_INITIAL_TEXT) ?: ""

        setContent {
            FinlinkTheme {
                var text by remember { mutableStateOf(initialText) }
                var editTextRef by remember { mutableStateOf<EditText?>(null) }

                fun submit() {
                    setResult(Activity.RESULT_OK, Intent().putExtra(EXTRA_RESULT_TEXT, text))
                    finish()
                }

                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    Scaffold(
                        topBar = {
                            TopAppBar(
                                title = { Text(stringResource(R.string.text_input_title)) },
                                navigationIcon = {
                                    TextButton(onClick = { finish() }) { Text(stringResource(R.string.cancel)) }
                                },
                                actions = {
                                    TextButton(onClick = { submit() }) { Text(stringResource(R.string.ok)) }
                                }
                            )
                        }
                    ) { innerPadding ->
                        Column(modifier = Modifier.padding(innerPadding).padding(20.dp).fillMaxSize()) {
                            AndroidView(
                                modifier = Modifier.fillMaxWidth(),
                                factory = { context ->
                                    EditText(context).apply {
                                        setText(initialText)
                                        setSelection(initialText.length)
                                        isSingleLine = true
                                        imeOptions = EditorInfo.IME_ACTION_DONE
                                        if (maxLength > 0) {
                                            filters = arrayOf(InputFilter.LengthFilter(maxLength))
                                        }
                                        addTextChangedListener(object : TextWatcher {
                                            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
                                            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
                                            override fun afterTextChanged(s: Editable?) {
                                                text = s?.toString().orEmpty()
                                            }
                                        })
                                        setOnEditorActionListener { _, actionId, _ ->
                                            if (actionId == EditorInfo.IME_ACTION_DONE) {
                                                submit()
                                                true
                                            } else {
                                                false
                                            }
                                        }
                                        editTextRef = this
                                    }
                                }
                            )
                            if (maxLength > 0) {
                                Spacer(Modifier.height(4.dp))
                                Text(
                                    "${text.length} / $maxLength",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    modifier = Modifier.align(Alignment.End)
                                )
                            }
                        }
                    }
                }

                LaunchedEffect(Unit) {
                    editTextRef?.let { editText ->
                        editText.requestFocus()
                        val imm = editText.context.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
                        imm.showSoftInput(editText, InputMethodManager.SHOW_IMPLICIT)
                    }
                }
            }
        }
    }
}
