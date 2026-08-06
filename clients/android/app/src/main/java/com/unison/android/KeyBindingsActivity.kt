package com.unison.android

import android.os.Bundle
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Column
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
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

/**
 * Physical key binding per button, split into two sections: GBA_BUTTONS
 * (every stream type that has any buttons at all understands these) and
 * EXT_BUTTONS/EXT_BUTTONS_LIMITED (only a hasButtonsMode session -- Azahar's
 * N3DS_BOTTOM_SCREEN today -- reads these; ZL/ZR/the stick directions within
 * that are flagged further still, since even among hasButtonsMode servers
 * most consoles don't have a second shoulder pair or care about them).
 * Split out of SettingsActivity into its own screen (this used to be one
 * inline section there) purely so that screen isn't dominated by what's
 * now three separate lists.
 *
 * Same dispatchKeyEvent-intercepts-the-next-press mechanism as before,
 * generalized over which of the two button types is currently pending via
 * [BindTarget].
 */
@OptIn(ExperimentalMaterial3Api::class)
class KeyBindingsActivity : LocalizedActivity() {

    private sealed class BindTarget {
        data class Gba(val button: GbaButton) : BindTarget()
        data class Ext(val button: ExtButton) : BindTarget()
    }

    private lateinit var prefs: Prefs
    private var pendingBindTarget: BindTarget? = null

    // Keyed by a type-prefixed string rather than the raw prefKey: GbaButton
    // and ExtButton draw from separate namespaces (Prefs.prefKeyFor uses
    // different SharedPreferences prefixes for exactly this reason) and can
    // share a label/prefKey, e.g. both have an "A".
    private val bindingTexts = mutableStateMapOf<String, String>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = Prefs(this)
        GBA_BUTTONS.forEach { bindingTexts[gbaKey(it)] = describeGbaBinding(it) }
        (EXT_BUTTONS + EXT_BUTTONS_LIMITED).forEach { bindingTexts[extKey(it)] = describeExtBinding(it) }

        setContent {
            UnisonTheme {
                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    Scaffold(
                        topBar = {
                            TopAppBar(
                                title = { Text(stringResource(R.string.settings_key_bindings)) },
                                navigationIcon = {
                                    TextButton(onClick = { finish() }) { Text(stringResource(R.string.back)) }
                                }
                            )
                        }
                    ) { innerPadding ->
                        LazyColumn(modifier = Modifier.padding(innerPadding).padding(horizontal = 16.dp).fillMaxSize()) {
                            item {
                                Spacer(Modifier.height(8.dp))
                                Text(stringResource(R.string.key_bindings_standard_section), style = MaterialTheme.typography.titleMedium)
                                Spacer(Modifier.height(8.dp))
                            }
                            items(GBA_BUTTONS) { button ->
                                BindingRow(
                                    label = button.label,
                                    bindingText = bindingTexts[gbaKey(button)] ?: "",
                                    onBind = {
                                        pendingBindTarget = BindTarget.Gba(button)
                                        bindingTexts[gbaKey(button)] = getString(R.string.settings_press_key)
                                    },
                                    onClear = {
                                        prefs.clearKeyBinding(button)
                                        bindingTexts[gbaKey(button)] = describeGbaBinding(button)
                                    }
                                )
                            }

                            item {
                                Spacer(Modifier.height(16.dp))
                                HorizontalDivider()
                                Spacer(Modifier.height(16.dp))
                                Text(stringResource(R.string.key_bindings_extended_section), style = MaterialTheme.typography.titleMedium)
                                Spacer(Modifier.height(4.dp))
                                Text(
                                    stringResource(R.string.key_bindings_extended_hint),
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                Spacer(Modifier.height(8.dp))
                            }
                            items(EXT_BUTTONS + EXT_BUTTONS_LIMITED) { button ->
                                BindingRow(
                                    label = button.label,
                                    bindingText = bindingTexts[extKey(button)] ?: "",
                                    onBind = {
                                        pendingBindTarget = BindTarget.Ext(button)
                                        bindingTexts[extKey(button)] = getString(R.string.settings_press_key)
                                    },
                                    onClear = {
                                        prefs.clearKeyBinding(button)
                                        bindingTexts[extKey(button)] = describeExtBinding(button)
                                    }
                                )
                            }
                            item { Spacer(Modifier.height(8.dp)) }
                        }
                    }
                }
            }
        }
    }

    @androidx.compose.runtime.Composable
    private fun BindingRow(label: String, bindingText: String, onBind: () -> Unit, onClear: () -> Unit) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp)
        ) {
            Text(label, modifier = Modifier.weight(1f))
            Text(
                bindingText,
                modifier = Modifier.padding(end = 8.dp),
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            TextButton(onClick = onBind) { Text(stringResource(R.string.settings_bind)) }
            TextButton(onClick = onClear) { Text(stringResource(R.string.settings_clear)) }
        }
        HorizontalDivider()
    }

    private fun gbaKey(button: GbaButton) = "gba:${button.prefKey}"
    private fun extKey(button: ExtButton) = "ext:${button.prefKey}"

    private fun describeGbaBinding(button: GbaButton): String {
        val code = prefs.getKeyBinding(button) ?: return getString(R.string.settings_unbound)
        return KeyEvent.keyCodeToString(code).removePrefix("KEYCODE_")
    }

    private fun describeExtBinding(button: ExtButton): String {
        val code = prefs.getKeyBinding(button) ?: return getString(R.string.settings_unbound)
        return KeyEvent.keyCodeToString(code).removePrefix("KEYCODE_")
    }

    /** Intercepts the next key press while a binding is pending, regardless
     * of which composable has focus. */
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        val target = pendingBindTarget
        if (target != null && event.action == KeyEvent.ACTION_DOWN) {
            when (target) {
                is BindTarget.Gba -> {
                    prefs.setKeyBinding(target.button, event.keyCode)
                    bindingTexts[gbaKey(target.button)] = describeGbaBinding(target.button)
                }
                is BindTarget.Ext -> {
                    prefs.setKeyBinding(target.button, event.keyCode)
                    bindingTexts[extKey(target.button)] = describeExtBinding(target.button)
                }
            }
            pendingBindTarget = null
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    /** Reported live: a D-pad press during a pending binding ALSO navigated
     * the interface (Compose focus visibly jumping between rows) at the
     * same time the button's own KeyEvent was being correctly captured
     * above -- only a second, separate press then produced a
     * clean-looking assignment. dispatchKeyEvent alone can't be the whole
     * story: it already unconditionally intercepts+consumes the very
     * first ACTION_DOWN while a bind is pending, so a KeyEvent-only leak
     * isn't possible. The other, entirely independent path a gamepad's
     * D-pad commonly ALSO reports through: a raw hat-switch MotionEvent
     * (AXIS_HAT_X/AXIS_HAT_Y), which is what Compose's own default
     * joystick-driven focus-navigation actually watches -- and which
     * nothing in this class was suppressing at all, letting it reach the
     * view hierarchy and move focus regardless of dispatchKeyEvent's own,
     * unrelated capture of the synthesized KeyEvent. Swallowing every
     * SOURCE_JOYSTICK/SOURCE_GAMEPAD generic motion event outright while a
     * binding is pending -- not just the hat axis specifically, since
     * there's no need to distinguish once a capture is already in
     * progress -- closes that off the same way dispatchKeyEvent already
     * closes off the KeyEvent side. */
    override fun onGenericMotionEvent(event: MotionEvent): Boolean {
        if (pendingBindTarget != null &&
            (event.source and (InputDevice.SOURCE_JOYSTICK or InputDevice.SOURCE_GAMEPAD)) != 0
        ) {
            return true
        }
        return super.onGenericMotionEvent(event)
    }
}
