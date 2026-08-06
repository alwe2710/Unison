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
 * Physical key binding per button. Used to be three separate sections
 * (GBA_BUTTONS's own Standard-Tasten list, then EXT_BUTTONS+
 * EXT_BUTTONS_LIMITED's Erweiterte-Tasten list under its own header/hint) --
 * merged per explicit request into one flat list in a fixed, hand-specified
 * order (A, B, X, Y, Start, Select, D-pad Up/Down/Left/Right, L, R, ZL, ZR)
 * that doesn't match either underlying list's own declaration order, hence
 * [unifiedBindOrder] rather than just concatenating GBA_BUTTONS with
 * EXT_BUTTONS/EXT_BUTTONS_LIMITED directly. GBA_BUTTONS/ExtButtons.kt's own
 * declaration order is left alone -- both lists are also used elsewhere
 * (PlayerActivity's on-screen touch row, Prefs' key-to-button maps) where
 * this screen's own display order has no business dictating anything.
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

    // See this class's own doc comment. ZL/ZR are EXT_BUTTONS_LIMITED's
    // remaining two entries (the eight STICK_* rows that used to also live
    // there were removed from that list entirely, see ExtButtons.kt's own
    // comment) -- X/Y come from EXT_BUTTONS.
    private val unifiedBindOrder: List<BindTarget> by lazy {
        val gbaByKey = GBA_BUTTONS.associateBy { it.prefKey }
        val extByKey = (EXT_BUTTONS + EXT_BUTTONS_LIMITED).associateBy { it.prefKey }
        listOf(
            BindTarget.Gba(gbaByKey.getValue("A")),
            BindTarget.Gba(gbaByKey.getValue("B")),
            BindTarget.Ext(extByKey.getValue("X")),
            BindTarget.Ext(extByKey.getValue("Y")),
            BindTarget.Gba(gbaByKey.getValue("START")),
            BindTarget.Gba(gbaByKey.getValue("SELECT")),
            BindTarget.Gba(gbaByKey.getValue("UP")),
            BindTarget.Gba(gbaByKey.getValue("DOWN")),
            BindTarget.Gba(gbaByKey.getValue("LEFT")),
            BindTarget.Gba(gbaByKey.getValue("RIGHT")),
            BindTarget.Gba(gbaByKey.getValue("L")),
            BindTarget.Gba(gbaByKey.getValue("R")),
            BindTarget.Ext(extByKey.getValue("ZL")),
            BindTarget.Ext(extByKey.getValue("ZR")),
        )
    }

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
                            item { Spacer(Modifier.height(8.dp)) }
                            items(unifiedBindOrder) { target ->
                                when (target) {
                                    is BindTarget.Gba -> BindingRow(
                                        label = target.button.label,
                                        bindingText = bindingTexts[gbaKey(target.button)] ?: "",
                                        onBind = {
                                            pendingBindTarget = target
                                            bindingTexts[gbaKey(target.button)] = getString(R.string.settings_press_key)
                                        },
                                        onClear = {
                                            prefs.clearKeyBinding(target.button)
                                            bindingTexts[gbaKey(target.button)] = describeGbaBinding(target.button)
                                        }
                                    )
                                    is BindTarget.Ext -> BindingRow(
                                        label = target.button.label,
                                        bindingText = bindingTexts[extKey(target.button)] ?: "",
                                        onBind = {
                                            pendingBindTarget = target
                                            bindingTexts[extKey(target.button)] = getString(R.string.settings_press_key)
                                        },
                                        onClear = {
                                            prefs.clearKeyBinding(target.button)
                                            bindingTexts[extKey(target.button)] = describeExtBinding(target.button)
                                        }
                                    )
                                }
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
        // TEMPORARY diagnostic (2026-08-06) -- see this file's own recent
        // history: two real fix attempts here still didn't resolve
        // controller binding on real hardware. Logs every event this
        // override actually sees. Remove once the real cause is found.
        android.util.Log.d("UnisonInputDiag", "KB.dispatchKeyEvent keyCode=${event.keyCode} " +
            "action=${event.action} source=0x${event.source.toString(16)} " +
            "device=${event.device?.name} pendingBindTarget=$pendingBindTarget")
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

    /** Real diagnostic evidence (2026-08-06, DualSense controller): a D-pad
     * press produces NO KeyEvent at all on this device -- only a raw
     * hat-switch MotionEvent (AXIS_HAT_X/AXIS_HAT_Y). dispatchKeyEvent
     * above, however correct in itself, therefore never sees a D-pad press
     * from this controller to capture in the first place -- confirmed live
     * via logcat (dispatchKeyEvent never fired for a press that
     * dispatchGenericMotionEvent very much did, HATY going to -1.0 and
     * back). A first attempt at this override only *suppressed* hat-axis
     * motion while a binding was pending (to stop it also driving Compose's
     * own joystick-focus-navigation); that stopped the interface from
     * visibly reacting, but did nothing to actually let a hat-only D-pad be
     * bound at all, since nothing was ever recorded for it. This version
     * captures a hat-axis crossing itself, synthesizing the same
     * KEYCODE_DPAD_UP/DOWN/LEFT/RIGHT identity a real D-pad KeyEvent would
     * have carried and storing *that* via prefs.setKeyBinding, exactly like
     * the KeyEvent path above does -- so PlayerActivity's own
     * keyCodeToBit/handleExtKey lookups (built from the same stored
     * KEYCODE_DPAD_* values) work identically regardless of which of the
     * two paths a given controller's D-pad actually uses. No repeat-capture
     * guard needed: once the first crossing sets pendingBindTarget to null,
     * every subsequent motion tick (this controller sends a continuous
     * stream, even at rest) simply finds nothing pending and falls through.
     *
     * dispatchGenericMotionEvent, not onGenericMotionEvent -- the exact
     * same fallback-vs-first-look mistake PlayerActivity's own analog-
     * stick fix already made and had to correct (see that class's own
     * comment): onGenericMotionEvent is only Android's fallback, called
     * after the DecorView/Compose hierarchy's dispatch already had first
     * crack. */
    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        val target = pendingBindTarget
        if (target == null || (event.source and (InputDevice.SOURCE_JOYSTICK or InputDevice.SOURCE_GAMEPAD)) == 0) {
            return super.dispatchGenericMotionEvent(event)
        }

        val hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X)
        val hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y)
        val keyCode = when {
            hatY < -0.5f -> KeyEvent.KEYCODE_DPAD_UP
            hatY > 0.5f -> KeyEvent.KEYCODE_DPAD_DOWN
            hatX < -0.5f -> KeyEvent.KEYCODE_DPAD_LEFT
            hatX > 0.5f -> KeyEvent.KEYCODE_DPAD_RIGHT
            else -> null
        }
        if (keyCode != null) {
            // TEMPORARY diagnostic (2026-08-06) -- only logs an actual
            // capture, not this controller's continuous at-rest stream.
            android.util.Log.d("UnisonInputDiag",
                "KB.dispatchGenericMotionEvent captured keyCode=$keyCode for $target (hat-axis, no KeyEvent existed)")
            when (target) {
                is BindTarget.Gba -> {
                    prefs.setKeyBinding(target.button, keyCode)
                    bindingTexts[gbaKey(target.button)] = describeGbaBinding(target.button)
                }
                is BindTarget.Ext -> {
                    prefs.setKeyBinding(target.button, keyCode)
                    bindingTexts[extKey(target.button)] = describeExtBinding(target.button)
                }
            }
            pendingBindTarget = null
        }
        return true
    }
}
