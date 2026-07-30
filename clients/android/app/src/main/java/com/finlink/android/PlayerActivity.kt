package com.finlink.android

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.os.Bundle
import android.view.KeyEvent
import android.view.View
import android.view.WindowManager
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.offset
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import android.content.Context
import android.text.Editable
import android.text.InputFilter
import android.text.TextWatcher
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import java.nio.ByteBuffer

/**
 * The actual stream view: connects to the host:port passed in via Intent
 * extras, shows video full-screen (ContentScale.Fit -- scales up, never
 * stretched, exactly like the old XML's fitCenter), plays audio, and
 * accepts input from both the on-screen button row (if enabled in Settings)
 * and any physical key/controller bindings set there. Owns the one
 * GbaStreamClient instance for its lifetime; MenuActivity and
 * SettingsActivity never touch it.
 *
 * Stays landscape-locked (AndroidManifest.xml) unlike Menu/Settings: the GBA
 * stream itself is a fixed wide aspect ratio.
 */
class PlayerActivity : LocalizedActivity(), GbaStreamClient.Listener {

    private lateinit var prefs: Prefs

    private var client: GbaStreamClient? = null
    private var audioTrack: AudioTrack? = null

    // Mic capture (GbaStreamClient.Listener.onMicEnable) -- only running
    // while the console has its mic powered on and sampling, see that
    // callback's own comment. Requested proactively in onCreate() rather
    // than lazily in onMicEnable() itself, since the server only resends
    // MIC_ENABLE(true) on an actual state change on its end (not
    // periodically) -- if permission were still ungranted the first time a
    // game asked for the mic, granting it later would have nothing to
    // retrigger capture until the game toggled its mic off and on again.
    private val requestRecordAudioPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { /* no-op either way, see class comment */ }
    private var micRecord: AudioRecord? = null
    private var micThread: Thread? = null
    @Volatile private var micStopFlag = false

    private var videoBitmap by mutableStateOf<Bitmap?>(null)
    private var statusText by mutableStateOf("")
    private var connected by mutableStateOf(false)
    private var disconnectedReason by mutableStateOf<String?>(null)
    private var onScreenControlsEnabled by mutableStateOf(true)
    private var bilinearVideoFilter by mutableStateOf(false)

    // Set from GbaStreamClient.Listener.onTextInputRequest() (the server's
    // own on-screen keyboard has no way to reach a remote client, see that
    // callback's own comment) -- non-null shows TextInputDialog below;
    // cleared again once the user submits or cancels.
    private data class TextInputRequest(val maxLength: Int, val initialText: String)
    private var textInputRequest by mutableStateOf<TextInputRequest?>(null)

    // Set from GbaStreamClient.Listener.onConnected(isTouch, hasButtons),
    // i.e. from the server's own hello.input_encoding -- not known before
    // that point, so both stay false (the GBA button overlay below) until
    // then regardless of what kind of server was actually dialed. See
    // TouchOverlay/ExtendedControlsOverlay for what each switches to.
    private var touchMode by mutableStateOf(false)
    private var hasButtonsMode by mutableStateOf(false)
    // Distinguishes melonDS's "touch_and_buttons" (buttons, no analog
    // sticks -- the DS has none on real hardware) from Azahar's
    // "n3ds_touch_and_buttons" (buttons AND circle pad/analog sticks).
    // Only meaningful when hasButtonsMode is true; gates whether
    // ExtDPad/VirtualStick/ZL/ZR are shown at all, see PlayerScreen().
    private var hasSticksMode by mutableStateOf(false)

    // Touch and physical-key input are tracked separately and OR'd together
    // when sent, so releasing one source doesn't clobber bits the other
    // source is still holding -- mirrors how the original web client merges
    // keyboard/touch/gamepad input (see docs/protocol.md's source notes).
    // Only meaningful in gba_buttons mode (!touchMode) -- see extTouchX and
    // friends below for touchMode's own, differently-shaped state.
    private var touchMask = 0
    private var physicalMask = 0
    private var keyCodeToBit: Map<Int, Int> = emptyMap()

    // Extended-input (touchMode && hasButtonsMode) state: touch, buttons,
    // and the circle pad/left stick all merge into ONE
    // finlink_extended_input frame per change (GbaStreamClient.
    // sendExtendedInput's own "one combined frame" design, unlike
    // gba_buttons' always-separate touch/key messages), via
    // sendCombinedExtendedInput() below -- so every source that changes any
    // one of these needs to re-send all of them together, not just its own
    // piece. Plain vars, not mutableStateOf: nothing here is read by a
    // composable, only ever written by gesture callbacks and read back by
    // this same function.
    private var extTouchPressed = false
    private var extTouchX = 0
    private var extTouchY = 0
    private var extButtons = 0 // on-screen ExtHoldButton contribution
    private var extPhysicalButtons = 0 // physical key contribution, see KeyBindingsActivity
    private var extStickLDragX = 0 // left VirtualStick's own (touch-drag) contribution
    private var extStickLDragY = 0
    private var extStickRDragX = 0 // right VirtualStick's own (touch-drag) contribution
    private var extStickRDragY = 0
    // Physical-key "digital stick" contribution -- held, each pushes that
    // stick to full deflection on that axis, same convention several other
    // emulators offer as a keyboard alternative to a real analog input;
    // combined with the matching VirtualStick's own drag contribution
    // (clamped addition) rather than one replacing the other, since both
    // could technically be held at once even though in practice a user
    // picks one input method or the other.
    private var extKeyStickLUp = false
    private var extKeyStickLDown = false
    private var extKeyStickLLeft = false
    private var extKeyStickLRight = false
    private var extKeyStickRUp = false
    private var extKeyStickRDown = false
    private var extKeyStickRLeft = false
    private var extKeyStickRRight = false
    private var extKeyCodeToButton: Map<Int, ExtButton> = emptyMap()
    // Standard-Tasten bindings that double as a hasButtonsMode button too
    // (ExtButtons.kt's GBA_PREFKEY_TO_EXT_BUTTON_BIT) -- see handleExtKey().
    private var keyCodeToExtBitFromGba: Map<Int, Int> = emptyMap()

    private fun sendCombinedExtendedInput() {
        // Y sign matches VirtualStick's own convention (see its comment):
        // positive = stick pushed up.
        fun axis(negative: Boolean, positive: Boolean) = when {
            negative == positive -> 0
            negative -> -32767
            else -> 32767
        }
        val leftX = (extStickLDragX + axis(extKeyStickLLeft, extKeyStickLRight)).coerceIn(-32768, 32767)
        val leftY = (extStickLDragY + axis(extKeyStickLDown, extKeyStickLUp)).coerceIn(-32768, 32767)
        val rightX = (extStickRDragX + axis(extKeyStickRLeft, extKeyStickRRight)).coerceIn(-32768, 32767)
        val rightY = (extStickRDragY + axis(extKeyStickRDown, extKeyStickRUp)).coerceIn(-32768, 32767)
        client?.sendExtendedInput(
            extTouchPressed, extTouchX, extTouchY,
            extButtons or extPhysicalButtons, leftX, leftY, rightX, rightY
        )
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableImmersiveMode()
        // Keep the screen on for as long as the stream is being watched --
        // otherwise the system dims/locks mid-session same as it would
        // during any other idle screen. Tied to this window, so it's lifted
        // automatically once the Activity is no longer shown.
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
            != PackageManager.PERMISSION_GRANTED) {
            requestRecordAudioPermission.launch(Manifest.permission.RECORD_AUDIO)
        }

        prefs = Prefs(this)
        keyCodeToBit = prefs.keyBindingsByKeyCode()
        extKeyCodeToButton = prefs.extKeyBindingsByKeyCode()
        keyCodeToExtBitFromGba = prefs.sharedExtButtonBitsByKeyCode()
        onScreenControlsEnabled = prefs.onScreenControlsEnabled
        // "" (manual host:port entry, see MenuActivity) reads as
        // Prefs.bilinearFor("")'s own not-yet-configured default (false,
        // nearest-neighbor) -- the real stream_type isn't known until the
        // handshake's hello message in that case, same limitation every
        // other client's manual-entry path already has.
        bilinearVideoFilter = prefs.bilinearFor(intent.getStringExtra(EXTRA_STREAM_TYPE) ?: "")

        setContent {
            FinlinkTheme {
                PlayerScreen()
            }
        }

        val host = intent.getStringExtra(EXTRA_HOST)
        val port = intent.getIntExtra(EXTRA_PORT, -1)
        if (host.isNullOrEmpty() || port <= 0) {
            statusText = getString(R.string.status_error, "kein Host übergeben")
            return
        }
        connectTo(host, port)
    }

    @Composable
    private fun PlayerScreen() {
        Surface(modifier = Modifier.fillMaxSize(), color = Color.Black) {
            Box(modifier = Modifier.fillMaxSize()) {
                videoBitmap?.let { bitmap ->
                    Image(
                        bitmap = bitmap.asImageBitmap(),
                        contentDescription = null,
                        contentScale = ContentScale.Fit,
                        // Bilinear (Low) smooths the upscale from the GBA's
                        // native 240x160; None gives nearest-neighbor, the
                        // crisp/pixelated look -- the Settings toggle.
                        filterQuality = if (bilinearVideoFilter) FilterQuality.Low else FilterQuality.None,
                        modifier = Modifier.fillMaxSize()
                    )

                    // Touch mode's only input method -- not gated on
                    // onScreenControlsEnabled (that preference is about the
                    // optional GBA button overlay below; a touch-based
                    // stream has no other way to provide input at all, so
                    // there's nothing to make optional here). Sized/aligned
                    // to this exact Image via the shared Box, and mapped
                    // through bitmap's own native pixel size -- always the
                    // current frame's actual dimensions (320x240 for
                    // N3DS_BOTTOM_SCREEN, 256x192 for NDS_BOTTOM_SCREEN,
                    // 854x480 for WIIU_GAMEPAD, ...), so this needs no
                    // per-stream-type table of its own.
                    if (touchMode) {
                        TouchOverlay(bitmap.width, bitmap.height, modifier = Modifier.fillMaxSize())
                    }
                }

                // Only shown before the stream is actually up (connecting,
                // or a pre-connect failure) -- once streaming, an unexpected
                // drop shows the dialog below instead, not this.
                if (!connected) {
                    Text(
                        statusText,
                        color = Color.White,
                        modifier = Modifier
                            .align(Alignment.TopStart)
                            .padding(8.dp)
                            .background(Color(0x80000000))
                            .padding(horizontal = 6.dp, vertical = 2.dp)
                    )
                }

                // No manual disconnect button: the system back button already
                // finishes this Activity, which tears the session down via
                // onDestroy() -> disconnect().

                // Mobile-emulator-style overlay, matching the web client's
                // layout (GBAStreamClientPage.h): shoulder buttons flush in
                // the top corners, Select/Start centered at the top between
                // them, D-pad bottom-left, A/B diagonal cluster bottom-right
                // -- offset like the real GBA's button placement, not a
                // plain row.
                if (onScreenControlsEnabled && !touchMode) {
                    GbaHoldButton(
                        "L", GbaStreamClient.KEY_L, shape = RoundedCornerShape(8.dp),
                        modifier = Modifier
                            .align(Alignment.TopStart)
                            .padding(top = 16.dp, start = 16.dp)
                            .size(width = 64.dp, height = 40.dp)
                    )
                    GbaHoldButton(
                        "R", GbaStreamClient.KEY_R, shape = RoundedCornerShape(8.dp),
                        modifier = Modifier
                            .align(Alignment.TopEnd)
                            .padding(top = 16.dp, end = 16.dp)
                            .size(width = 64.dp, height = 40.dp)
                    )

                    Row(
                        modifier = Modifier.align(Alignment.TopCenter).padding(top = 16.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        GbaHoldButton(
                            "Select", GbaStreamClient.KEY_SELECT,
                            shape = RoundedCornerShape(50),
                            modifier = Modifier.size(width = 64.dp, height = 28.dp)
                        )
                        GbaHoldButton(
                            "Start", GbaStreamClient.KEY_START,
                            shape = RoundedCornerShape(50),
                            modifier = Modifier.size(width = 64.dp, height = 28.dp)
                        )
                    }

                    DPad(modifier = Modifier.align(Alignment.BottomStart).padding(24.dp))
                    ActionButtons(modifier = Modifier.align(Alignment.BottomEnd).padding(24.dp))
                }

                // Buttons for a hasButtonsMode session -- shown alongside
                // TouchOverlay above (not instead of it), since these and
                // touch are independent parts of one combined
                // finlink_extended_input/finlink_touch_and_buttons frame,
                // not alternatives (see extTouchPressed's own comment).
                // Same L/R/Select/Start layout as the gba_buttons overlay,
                // via ExtHoldButton instead of GbaHoldButton; X/Y (a real
                // 3DS/Wii U/DS button the GBA overlay has no equivalent
                // for) added to the A/B cluster. ZL/ZR and both sticks
                // (real analog, VirtualStick, rather than a digital cross)
                // only for hasSticksMode; otherwise (melonDS's DS session,
                // no analog input on real hardware) ExtDPad below stands
                // in for the missing left stick.
                if (touchMode && hasButtonsMode) {
                    ExtHoldButton(
                        "L", GbaStreamClient.BUTTON_L, shape = RoundedCornerShape(8.dp),
                        modifier = Modifier
                            .align(Alignment.TopStart)
                            .padding(top = 16.dp, start = 16.dp)
                            .size(width = 64.dp, height = 40.dp)
                    )
                    ExtHoldButton(
                        "R", GbaStreamClient.BUTTON_R, shape = RoundedCornerShape(8.dp),
                        modifier = Modifier
                            .align(Alignment.TopEnd)
                            .padding(top = 16.dp, end = 16.dp)
                            .size(width = 64.dp, height = 40.dp)
                    )

                    Row(
                        modifier = Modifier.align(Alignment.TopCenter).padding(top = 16.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        ExtHoldButton(
                            "Select", GbaStreamClient.BUTTON_SELECT,
                            shape = RoundedCornerShape(50),
                            modifier = Modifier.size(width = 64.dp, height = 28.dp)
                        )
                        ExtHoldButton(
                            "Start", GbaStreamClient.BUTTON_START,
                            shape = RoundedCornerShape(50),
                            modifier = Modifier.size(width = 64.dp, height = 28.dp)
                        )
                    }

                    ExtActionButtons(modifier = Modifier.align(Alignment.BottomEnd).padding(24.dp))

                    // ZL/ZR and both analog sticks only exist on hardware
                    // that actually has them (Azahar's N3DS_BOTTOM_SCREEN,
                    // "n3ds_touch_and_buttons") -- melonDS's DS session
                    // ("touch_and_buttons", hasSticksMode = false) gets a
                    // D-pad instead below, since real DS hardware has
                    // neither ZL/ZR nor any analog input at all.
                    if (hasSticksMode) {
                        ExtHoldButton(
                            "ZL", GbaStreamClient.BUTTON_ZL, shape = RoundedCornerShape(8.dp),
                            modifier = Modifier
                                .align(Alignment.TopStart)
                                .padding(top = 60.dp, start = 16.dp)
                                .size(width = 64.dp, height = 40.dp)
                        )
                        ExtHoldButton(
                            "ZR", GbaStreamClient.BUTTON_ZR, shape = RoundedCornerShape(8.dp),
                            modifier = Modifier
                                .align(Alignment.TopEnd)
                                .padding(top = 60.dp, end = 16.dp)
                                .size(width = 64.dp, height = 40.dp)
                        )

                        VirtualStick(
                            modifier = Modifier.align(Alignment.BottomStart).padding(24.dp),
                            onMove = { x, y -> extStickLDragX = x; extStickLDragY = y; sendCombinedExtendedInput() },
                            onRelease = { extStickLDragX = 0; extStickLDragY = 0; sendCombinedExtendedInput() }
                        )
                        // Top-end, left of the R/ZR column rather than
                        // CenterEnd -- centered vertically overlapped
                        // ExtActionButtons' A/B/X/Y diamond down in the
                        // BottomEnd corner.
                        VirtualStick(
                            modifier = Modifier.align(Alignment.TopEnd).padding(top = 16.dp, end = 96.dp),
                            onMove = { x, y -> extStickRDragX = x; extStickRDragY = y; sendCombinedExtendedInput() },
                            onRelease = { extStickRDragX = 0; extStickRDragY = 0; sendCombinedExtendedInput() }
                        )
                    } else {
                        ExtDPad(modifier = Modifier.align(Alignment.BottomStart).padding(24.dp))
                    }
                }
            }

            // onDisconnected() only ever fires for a drop the session didn't
            // ask for (handshake/connect failure, peer closed, protocol
            // error) -- a user-initiated exit goes through the system back
            // button -> finish() -> onDestroy() directly, never through
            // here. So every time this shows, it's genuinely unexpected.
            // Custom Dialog instead of AlertDialog: the default M3 AlertDialog
            // reserves generous fixed spacing between the message and the
            // button row that looked like dead space for a one-line reason.
            disconnectedReason?.let { reason ->
                Dialog(onDismissRequest = { finish() }) {
                    Surface(shape = RoundedCornerShape(16.dp), color = MaterialTheme.colorScheme.surface) {
                        Column(modifier = Modifier.padding(20.dp)) {
                            Text(stringResource(R.string.stream_lost_title), style = MaterialTheme.typography.titleMedium)
                            Spacer(Modifier.height(8.dp))
                            Text(reason, style = MaterialTheme.typography.bodyMedium)
                            Spacer(Modifier.height(12.dp))
                            TextButton(onClick = { finish() }, modifier = Modifier.align(Alignment.End)) {
                                Text(stringResource(R.string.ok))
                            }
                        }
                    }
                }
            }

            // See Listener.onTextInputRequest()'s own comment for why this
            // exists at all: the server's own on-screen keyboard has no way
            // to reach a remote client.
            textInputRequest?.let { request ->
                TextInputDialog(
                    request = request,
                    onSubmit = { text ->
                        client?.sendTextInputResponse(true, text)
                        textInputRequest = null
                    },
                    onCancel = {
                        client?.sendTextInputResponse(false, "")
                        textInputRequest = null
                    }
                )
            }
        }
    }

    /** Pre-filled with request.initialText, auto-focused so the system
     * keyboard comes up immediately -- the whole point of this dialog is to
     * stand in for the server's own on-screen keyboard, which the video
     * stream never shows.
     *
     * Backed by a real android.widget.EditText via AndroidView rather than
     * Compose's own TextField/OutlinedTextField: Compose's text fields
     * always set IME_FLAG_NO_EXTRACT_UI on their InputConnection, which
     * permanently suppresses the keyboard's own native fullscreen "extract
     * mode" input surface. A plain EditText doesn't set that flag, so in
     * landscape (this stream's usual orientation) the system keyboard takes
     * over the whole screen itself with its own real fullscreen editor --
     * the actual OS input experience instead of an app-drawn imitation of
     * one. IME "Done" submits the same as the OK button; dismissing (back
     * button) cancels, same as Cancel. */
    @Composable
    private fun TextInputDialog(request: TextInputRequest, onSubmit: (String) -> Unit, onCancel: () -> Unit) {
        var text by remember(request) { mutableStateOf(request.initialText) }
        var editTextRef by remember { mutableStateOf<EditText?>(null) }

        Dialog(
            onDismissRequest = onCancel,
            properties = DialogProperties(usePlatformDefaultWidth = false)
        ) {
            Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.surface) {
                Column(modifier = Modifier.fillMaxSize().padding(20.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                        TextButton(onClick = onCancel) { Text(stringResource(R.string.cancel)) }
                        Text(
                            stringResource(R.string.text_input_title),
                            style = MaterialTheme.typography.titleMedium,
                            modifier = Modifier.weight(1f).padding(horizontal = 8.dp)
                        )
                        TextButton(onClick = { onSubmit(text) }) { Text(stringResource(R.string.ok)) }
                    }
                    Spacer(Modifier.height(24.dp))
                    AndroidView(
                        modifier = Modifier.fillMaxWidth(),
                        factory = { context ->
                            EditText(context).apply {
                                setText(request.initialText)
                                setSelection(request.initialText.length)
                                isSingleLine = true
                                imeOptions = EditorInfo.IME_ACTION_DONE
                                if (request.maxLength > 0) {
                                    filters = arrayOf(InputFilter.LengthFilter(request.maxLength))
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
                                        onSubmit(text)
                                        true
                                    } else {
                                        false
                                    }
                                }
                                editTextRef = this
                            }
                        }
                    )
                    if (request.maxLength > 0) {
                        Spacer(Modifier.height(4.dp))
                        Text(
                            "${text.length} / ${request.maxLength}",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.align(Alignment.End)
                        )
                    }
                }
            }
        }

        LaunchedEffect(request) {
            editTextRef?.let { editText ->
                editText.requestFocus()
                val imm = editText.context.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
                imm.showSoftInput(editText, InputMethodManager.SHOW_IMPLICIT)
            }
        }
    }

    /** Cross-shaped D-pad: only the four edge-center cells of a 3x3 grid are
     * filled, which alone reads as a plus/cross, matching a real D-pad
     * instead of four buttons in a row.
     *
     * Unlike every other on-screen button (HoldButton below, independent
     * per-button gestures), the D-pad is one continuous touch area with a
     * single gesture: dragging from one direction into an adjacent one
     * without lifting the finger switches the pressed key, the way a real
     * D-pad thumb-slide would. Four separate HoldButtons can't do this --
     * each one's detectTapGestures(onPress) keeps following the same
     * pointer whichever way it later moves (Compose tracks it by ID, not by
     * current position), so a slide off one button's bounds never reaches
     * the neighboring button's own, never-started gesture. */
    /** The whole visible video area doubles as the touch surface: press,
     * drag, and release all map 1:1 to finlink_touch_state's pressed/x/y
     * (protocol.h) via sendMappedTouch() below. One continuous gesture like
     * DPad's own -- a drag needs to keep reporting positions all the way to
     * release, not just an initial tap. */
    @Composable
    private fun TouchOverlay(bitmapWidth: Int, bitmapHeight: Int, modifier: Modifier = Modifier) {
        Box(
            modifier = modifier
                .pointerInput(bitmapWidth, bitmapHeight) {
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        sendMappedTouch(down.position, size, bitmapWidth, bitmapHeight)

                        while (true) {
                            val event = awaitPointerEvent()
                            val change = event.changes.firstOrNull { it.id == down.id } ?: break
                            if (!change.pressed) break
                            sendMappedTouch(change.position, size, bitmapWidth, bitmapHeight)
                            change.consume()
                        }

                        sendTouchState(pressed = false, x = 0, y = 0)
                    }
                }
        )
    }

    /** Routes a touch update through sendTouch (plain "n3ds_touch" session)
     * or sendCombinedExtendedInput (hasButtonsMode, which needs the current
     * button/stick state resent alongside every touch change too, not just
     * touch's own) -- see extTouchPressed and friends' own comment for why
     * touch can't just be sent on its own once buttons are involved. */
    private fun sendTouchState(pressed: Boolean, x: Int, y: Int) {
        if (hasButtonsMode) {
            extTouchPressed = pressed
            extTouchX = x
            extTouchY = y
            sendCombinedExtendedInput()
        } else {
            client?.sendTouch(pressed, x, y)
        }
    }

    /** Maps a tap position (in this Box's own pixel coordinates) through
     * ContentScale.Fit's letterboxing math to the video bitmap's native
     * pixel coordinates -- the same "centered, scaled to fit, aspect
     * preserved" placement the Image displaying that same bitmap already
     * uses right underneath this overlay, see PlayerScreen(). A tap that
     * lands in the letterbox bars (mismatched container/content aspect
     * ratio) clamps to the nearest edge rather than being dropped, so a
     * drag that wanders there still tracks instead of going silent. */
    private fun sendMappedTouch(position: Offset, containerSize: IntSize, bitmapWidth: Int, bitmapHeight: Int) {
        val containerW = containerSize.width.toFloat()
        val containerH = containerSize.height.toFloat()
        if (containerW <= 0f || containerH <= 0f || bitmapWidth <= 0 || bitmapHeight <= 0) return

        val scale = minOf(containerW / bitmapWidth, containerH / bitmapHeight)
        val offsetX = (containerW - bitmapWidth * scale) / 2f
        val offsetY = (containerH - bitmapHeight * scale) / 2f

        val x = ((position.x - offsetX) / scale).toInt().coerceIn(0, bitmapWidth - 1)
        val y = ((position.y - offsetY) / scale).toInt().coerceIn(0, bitmapHeight - 1)
        sendTouchState(pressed = true, x = x, y = y)
    }

    @Composable
    private fun DPad(modifier: Modifier = Modifier) {
        val segment = 56.dp
        val dpadMask = GbaStreamClient.KEY_UP or GbaStreamClient.KEY_DOWN or
            GbaStreamClient.KEY_LEFT or GbaStreamClient.KEY_RIGHT

        Box(
            modifier = modifier
                .size(segment * 3)
                .pointerInput(Unit) {
                    val segmentPx = segment.toPx()

                    // Which of the 3x3 grid's cells `position` (local to this
                    // Box) falls into -- the four edge cells map to a
                    // direction, the empty corners/center map to "nothing
                    // pressed" rather than snapping to the nearest direction,
                    // so a drag through a corner briefly releases instead of
                    // guessing which of the two adjacent directions was meant.
                    fun bitsAt(position: Offset): Int {
                        val col = (position.x / segmentPx).toInt().coerceIn(0, 2)
                        val row = (position.y / segmentPx).toInt().coerceIn(0, 2)
                        return when {
                            col == 1 && row == 0 -> GbaStreamClient.KEY_UP
                            col == 1 && row == 2 -> GbaStreamClient.KEY_DOWN
                            col == 0 && row == 1 -> GbaStreamClient.KEY_LEFT
                            col == 2 && row == 1 -> GbaStreamClient.KEY_RIGHT
                            else -> 0
                        }
                    }

                    awaitEachGesture {
                        val down = awaitFirstDown()
                        var activeBits = bitsAt(down.position)
                        touchMask = (touchMask and dpadMask.inv()) or activeBits
                        sendCombinedInput()

                        while (true) {
                            val event = awaitPointerEvent()
                            val change = event.changes.firstOrNull { it.id == down.id } ?: break
                            if (!change.pressed) break
                            val newBits = bitsAt(change.position)
                            if (newBits != activeBits) {
                                activeBits = newBits
                                touchMask = (touchMask and dpadMask.inv()) or activeBits
                                sendCombinedInput()
                            }
                            change.consume()
                        }

                        touchMask = touchMask and dpadMask.inv()
                        sendCombinedInput()
                    }
                }
        ) {
            DPadCell("▲", modifier = Modifier.align(Alignment.TopCenter).size(segment))
            DPadCell("▼", modifier = Modifier.align(Alignment.BottomCenter).size(segment))
            DPadCell("◀", modifier = Modifier.align(Alignment.CenterStart).size(segment))
            DPadCell("▶", modifier = Modifier.align(Alignment.CenterEnd).size(segment))
        }
    }

    /** DPad's counterpart for a hasButtonsMode-without-sticks session
     * (melonDS's "touch_and_buttons" -- the DS has a D-pad but no analog
     * stick at all, unlike Azahar's N3DS_BOTTOM_SCREEN which gets
     * VirtualStick instead, see PlayerScreen()). Same 3x3-grid gesture as
     * DPad, but writes into extButtons/sendCombinedExtendedInput() (the
     * combined-frame state every other hasButtonsMode control already
     * shares) instead of touchMask/sendCombinedInput(), since this is the
     * shared touch_and_buttons/n3ds_touch_and_buttons wire path, not the
     * separate gba_buttons one DPad serves. */
    @Composable
    private fun ExtDPad(modifier: Modifier = Modifier) {
        val segment = 56.dp
        val dpadMask = GbaStreamClient.BUTTON_UP or GbaStreamClient.BUTTON_DOWN or
            GbaStreamClient.BUTTON_LEFT or GbaStreamClient.BUTTON_RIGHT

        Box(
            modifier = modifier
                .size(segment * 3)
                .pointerInput(Unit) {
                    val segmentPx = segment.toPx()

                    fun bitsAt(position: Offset): Int {
                        val col = (position.x / segmentPx).toInt().coerceIn(0, 2)
                        val row = (position.y / segmentPx).toInt().coerceIn(0, 2)
                        return when {
                            col == 1 && row == 0 -> GbaStreamClient.BUTTON_UP
                            col == 1 && row == 2 -> GbaStreamClient.BUTTON_DOWN
                            col == 0 && row == 1 -> GbaStreamClient.BUTTON_LEFT
                            col == 2 && row == 1 -> GbaStreamClient.BUTTON_RIGHT
                            else -> 0
                        }
                    }

                    awaitEachGesture {
                        val down = awaitFirstDown()
                        var activeBits = bitsAt(down.position)
                        extButtons = (extButtons and dpadMask.inv()) or activeBits
                        sendCombinedExtendedInput()

                        while (true) {
                            val event = awaitPointerEvent()
                            val change = event.changes.firstOrNull { it.id == down.id } ?: break
                            if (!change.pressed) break
                            val newBits = bitsAt(change.position)
                            if (newBits != activeBits) {
                                activeBits = newBits
                                extButtons = (extButtons and dpadMask.inv()) or activeBits
                                sendCombinedExtendedInput()
                            }
                            change.consume()
                        }

                        extButtons = extButtons and dpadMask.inv()
                        sendCombinedExtendedInput()
                    }
                }
        ) {
            DPadCell("▲", modifier = Modifier.align(Alignment.TopCenter).size(segment))
            DPadCell("▼", modifier = Modifier.align(Alignment.BottomCenter).size(segment))
            DPadCell("◀", modifier = Modifier.align(Alignment.CenterStart).size(segment))
            DPadCell("▶", modifier = Modifier.align(Alignment.CenterEnd).size(segment))
        }
    }

    /** Purely visual D-pad cell -- press state is driven entirely by DPad's
     * own single gesture above, not by anything attached here. */
    @Composable
    private fun DPadCell(label: String, modifier: Modifier = Modifier) {
        Box(
            modifier = modifier
                .background(MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.85f),
                    RoundedCornerShape(6.dp)),
            contentAlignment = Alignment.Center
        ) {
            Text(label, color = MaterialTheme.colorScheme.onSecondaryContainer)
        }
    }

    /** B bottom-left, A top-end within the same cluster -- the diagonal
     * offset the real GBA has between the two, not side-by-side. */
    @Composable
    private fun ActionButtons(modifier: Modifier = Modifier) {
        val size = 72.dp
        Box(modifier = modifier.size(width = size * 2, height = size * 1.6f)) {
            GbaHoldButton(
                "B", GbaStreamClient.KEY_B, shape = CircleShape,
                modifier = Modifier.align(Alignment.BottomStart).size(size)
            )
            GbaHoldButton(
                "A", GbaStreamClient.KEY_A, shape = CircleShape,
                modifier = Modifier.align(Alignment.TopEnd).size(size)
            )
        }
    }

    /** A/B/X/Y diamond (X top, Y left, A right, B bottom) -- the real 3DS
     * face button layout, unlike ActionButtons' GBA-only A/B pair. */
    @Composable
    private fun ExtActionButtons(modifier: Modifier = Modifier) {
        val size = 64.dp
        Box(modifier = modifier.size(size * 3)) {
            ExtHoldButton(
                "X", GbaStreamClient.BUTTON_X, shape = CircleShape,
                modifier = Modifier.align(Alignment.TopCenter).size(size)
            )
            ExtHoldButton(
                "Y", GbaStreamClient.BUTTON_Y, shape = CircleShape,
                modifier = Modifier.align(Alignment.CenterStart).size(size)
            )
            ExtHoldButton(
                "A", GbaStreamClient.BUTTON_A, shape = CircleShape,
                modifier = Modifier.align(Alignment.CenterEnd).size(size)
            )
            ExtHoldButton(
                "B", GbaStreamClient.BUTTON_B, shape = CircleShape,
                modifier = Modifier.align(Alignment.BottomCenter).size(size)
            )
        }
    }

    /** Real analog stick -- for the circle pad, or on a two-stick console
     * (WIIU_GAMEPAD) either its left or right stick, one instance each (see
     * PlayerScreen()'s two call sites). Drag from anywhere inside, position
     * clamps to the outer circle's radius, releasing snaps back to (0, 0).
     * Reports through [onMove]/[onRelease] in the wire's -32768..32767
     * range rather than writing a hardcoded field directly, so this same
     * composable serves both sticks; the caller's callback both updates the
     * matching extStick{L,R}DragX/Y pair and resends the whole combined
     * frame (sendCombinedExtendedInput(), same "resend everything together"
     * reasoning as sendTouchState/ExtHoldButton).
     *
     * Y sign is inverted (screen-down is positive, but "stick pushed up"
     * should be a positive Y like a real circle pad) -- unverified against
     * real hardware, flip this if it turns out backwards in practice. */
    @Composable
    private fun VirtualStick(modifier: Modifier = Modifier, onMove: (x: Int, y: Int) -> Unit, onRelease: () -> Unit) {
        val outerDiameter = 112.dp
        var knobOffset by remember { mutableStateOf(Offset.Zero) }
        Box(
            modifier = modifier
                .size(outerDiameter)
                .background(MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.5f), CircleShape)
                .pointerInput(Unit) {
                    val radiusPx = outerDiameter.toPx() / 2f
                    val center = Offset(radiusPx, radiusPx)

                    fun report(position: Offset) {
                        val delta = position - center
                        val distance = delta.getDistance()
                        val clamped = if (distance > radiusPx) delta * (radiusPx / distance) else delta
                        knobOffset = clamped
                        val x = (clamped.x / radiusPx * 32767f).toInt().coerceIn(-32767, 32767)
                        val y = (-clamped.y / radiusPx * 32767f).toInt().coerceIn(-32767, 32767)
                        onMove(x, y)
                    }

                    awaitEachGesture {
                        val down = awaitFirstDown()
                        report(down.position)
                        while (true) {
                            val event = awaitPointerEvent()
                            val change = event.changes.firstOrNull { it.id == down.id } ?: break
                            if (!change.pressed) break
                            report(change.position)
                            change.consume()
                        }
                        knobOffset = Offset.Zero
                        onRelease()
                    }
                },
            contentAlignment = Alignment.Center
        ) {
            Box(
                modifier = Modifier
                    .offset { IntOffset(knobOffset.x.toInt(), knobOffset.y.toInt()) }
                    .size(outerDiameter / 2)
                    .background(MaterialTheme.colorScheme.secondaryContainer, CircleShape)
            )
        }
    }

    /** Plain Button/clickable() only fires on release-tap; a held button
     * needs a real press/release pair (held = keeps sending the bit), hence
     * detectTapGestures(onPress) + awaitRelease() instead. onPress/onRelease
     * rather than a hardcoded bit+touchMask write, so this same composable
     * serves both gba_buttons' overlay (touchMask) and the extended-input
     * overlay's (extButtons) -- see their respective call sites. */
    @Composable
    private fun HoldButton(
        label: String, onPress: () -> Unit, onRelease: () -> Unit, shape: Shape,
        modifier: Modifier = Modifier
    ) {
        Box(
            modifier = modifier
                .background(MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.85f), shape)
                .pointerInput(Unit) {
                    detectTapGestures(onPress = {
                        onPress()
                        try {
                            awaitRelease()
                        } finally {
                            onRelease()
                        }
                    })
                },
            contentAlignment = Alignment.Center
        ) {
            Text(label, color = MaterialTheme.colorScheme.onSecondaryContainer)
        }
    }

    /** gba_buttons-mode HoldButton: writes bit into touchMask, exactly the
     * previous hardcoded behavior, just expressed as onPress/onRelease now
     * that HoldButton itself takes those instead. */
    @Composable
    private fun GbaHoldButton(label: String, bit: Int, shape: Shape, modifier: Modifier = Modifier) {
        HoldButton(
            label,
            onPress = { touchMask = touchMask or bit; sendCombinedInput() },
            onRelease = { touchMask = touchMask and bit.inv(); sendCombinedInput() },
            shape, modifier
        )
    }

    /** Extended-input-mode HoldButton: writes bit into extButtons and resends
     * the whole combined frame (touch + buttons + stick), same reasoning as
     * sendTouchState -- see extTouchPressed's own comment. */
    @Composable
    private fun ExtHoldButton(label: String, bit: Int, shape: Shape, modifier: Modifier = Modifier) {
        HoldButton(
            label,
            onPress = { extButtons = extButtons or bit; sendCombinedExtendedInput() },
            onRelease = { extButtons = extButtons and bit.inv(); sendCombinedExtendedInput() },
            shape, modifier
        )
    }

    @Suppress("DEPRECATION")
    private fun enableImmersiveMode() {
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            )
    }

    @Suppress("DEPRECATION")
    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) enableImmersiveMode()
    }

    // Physical keyboard/controller input, per the bindings set in
    // SettingsActivity. Unbound keys fall through to the system default
    // (e.g. volume/back keys keep working).

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (hasButtonsMode && handleExtKey(keyCode, pressed = true)) return true
        val bit = keyCodeToBit[keyCode] ?: return super.onKeyDown(keyCode, event)
        physicalMask = physicalMask or bit
        sendCombinedInput()
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (hasButtonsMode && handleExtKey(keyCode, pressed = false)) return true
        val bit = keyCodeToBit[keyCode] ?: return super.onKeyUp(keyCode, event)
        physicalMask = physicalMask and bit.inv()
        sendCombinedInput()
        return true
    }

    private fun sendCombinedInput() {
        client?.sendInput(touchMask or physicalMask)
    }

    /** Applies one physical-key press/release for a hasButtonsMode session,
     * checking both binding sources: extKeyCodeToButton (X/Y/Home/ZL/ZR/
     * stick, bound in KeyBindingsActivity's own "Erweiterte Tasten"
     * section) and keyCodeToExtBitFromGba (A/B/L/R/Select/Start/Up/Down/
     * Left/Right, reusing the Standard-Tasten binding instead of asking for
     * it twice -- see ExtButtons.kt's GBA_PREFKEY_TO_EXT_BUTTON_BIT).
     * Returns whether keyCode matched either, so the caller knows not to
     * also fall through to the gba_buttons path below. */
    private fun handleExtKey(keyCode: Int, pressed: Boolean): Boolean {
        var handled = false
        extKeyCodeToButton[keyCode]?.let { applyExtKey(it, pressed); handled = true }
        keyCodeToExtBitFromGba[keyCode]?.let {
            applyExtButtonBit(it, pressed)
            sendCombinedExtendedInput()
            handled = true
        }
        return handled
    }

    /** A BUTTON entry ORs/ANDs its bit into extPhysicalButtons exactly like
     * ExtHoldButton's on-screen counterpart; a STICK_* entry just flips the
     * corresponding held-direction flag, see sendCombinedExtendedInput's own
     * digital-stick math. */
    private fun applyExtKey(button: ExtButton, pressed: Boolean) {
        when (button.kind) {
            ExtInputKind.BUTTON -> applyExtButtonBit(button.bit, pressed)
            ExtInputKind.STICK_L_UP -> extKeyStickLUp = pressed
            ExtInputKind.STICK_L_DOWN -> extKeyStickLDown = pressed
            ExtInputKind.STICK_L_LEFT -> extKeyStickLLeft = pressed
            ExtInputKind.STICK_L_RIGHT -> extKeyStickLRight = pressed
            ExtInputKind.STICK_R_UP -> extKeyStickRUp = pressed
            ExtInputKind.STICK_R_DOWN -> extKeyStickRDown = pressed
            ExtInputKind.STICK_R_LEFT -> extKeyStickRLeft = pressed
            ExtInputKind.STICK_R_RIGHT -> extKeyStickRRight = pressed
        }
        sendCombinedExtendedInput()
    }

    private fun applyExtButtonBit(bit: Int, pressed: Boolean) {
        extPhysicalButtons = if (pressed) extPhysicalButtons or bit else extPhysicalButtons and bit.inv()
    }

    private fun connectTo(host: String, port: Int) {
        touchMask = 0
        physicalMask = 0
        connected = false
        disconnectedReason = null
        textInputRequest = null
        touchMode = false
        hasButtonsMode = false
        hasSticksMode = false
        extTouchPressed = false
        extButtons = 0
        extPhysicalButtons = 0
        extStickLDragX = 0
        extStickLDragY = 0
        extStickRDragX = 0
        extStickRDragY = 0
        extKeyStickLUp = false
        extKeyStickLDown = false
        extKeyStickLLeft = false
        extKeyStickLRight = false
        extKeyStickRUp = false
        extKeyStickRDown = false
        extKeyStickRLeft = false
        extKeyStickRRight = false
        val c = GbaStreamClient(this)
        client = c
        statusText = getString(R.string.status_connecting)
        c.connect(host, port)
    }

    private fun disconnect() {
        client?.disconnect()
        client = null
        stopAudio()
    }

    private fun stopAudio() {
        audioTrack?.stop()
        audioTrack?.release()
        audioTrack = null
    }

    // --- GbaStreamClient.Listener: called from the native session thread,
    // never the UI thread, so every callback must hop back via runOnUiThread
    // before touching Compose state. onAudioFrame is the one exception --
    // writing to AudioTrack from a background thread is exactly what it's for.

    override fun onConnected(isTouch: Boolean, hasButtons: Boolean, hasSticks: Boolean) {
        runOnUiThread {
            connected = true
            touchMode = isTouch
            hasButtonsMode = hasButtons
            hasSticksMode = hasSticks
        }
    }

    override fun onTextInputRequest(maxLength: Int, initialText: String) {
        runOnUiThread {
            textInputRequest = TextInputRequest(maxLength, initialText)
        }
    }

    override fun onVideoFrame(width: Int, height: Int, rgb565: ByteArray) {
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.RGB_565)
        bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(rgb565))
        runOnUiThread { videoBitmap = bitmap }
    }

    override fun onAudioFrame(sampleRate: Int, channels: Int, pcm: ShortArray) {
        var track = audioTrack
        if (track == null || track.sampleRate != sampleRate) {
            track?.stop()
            track?.release()
            val channelConfig =
                if (channels >= 2) AudioFormat.CHANNEL_OUT_STEREO else AudioFormat.CHANNEL_OUT_MONO
            val minBufSize =
                AudioTrack.getMinBufferSize(sampleRate, channelConfig, AudioFormat.ENCODING_PCM_16BIT)
            track = AudioTrack(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build(),
                AudioFormat.Builder()
                    .setSampleRate(sampleRate)
                    .setChannelMask(channelConfig)
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .build(),
                maxOf(minBufSize, 4096) * 2,
                AudioTrack.MODE_STREAM,
                AudioManager.AUDIO_SESSION_ID_GENERATE
            )
            track.play()
            audioTrack = track
        }
        track.write(pcm, 0, pcm.size)
    }

    override fun onMicEnable(enabled: Boolean, sampleRate: Int) {
        if (enabled) startMicCapture(sampleRate) else stopMicCapture()
    }

    // Idempotent (stops any already-running capture first) since a change
    // in the requested sample rate arrives as another onMicEnable(true,
    // ...) rather than a separate message. Silently does nothing if
    // RECORD_AUDIO isn't granted, the device can't open this config, or
    // AudioRecord itself fails to initialize -- see GbaStreamClient.
    // Listener.onMicEnable's own comment on why that's the right behavior
    // here rather than surfacing an error.
    private fun startMicCapture(sampleRate: Int) {
        stopMicCapture()
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
            != PackageManager.PERMISSION_GRANTED) {
            return
        }
        val minBufSize = AudioRecord.getMinBufferSize(
            sampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT
        )
        if (minBufSize <= 0) return // Device can't do this sample rate/config at all.

        val record = try {
            AudioRecord(
                MediaRecorder.AudioSource.MIC,
                sampleRate,
                AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT,
                maxOf(minBufSize, 4096) * 2
            )
        } catch (e: SecurityException) {
            return // Permission revoked between the check above and here.
        }
        if (record.state != AudioRecord.STATE_INITIALIZED) {
            record.release()
            return
        }

        micStopFlag = false
        micRecord = record
        record.startRecording()
        val thread = Thread {
            val buf = ShortArray(1024)
            while (!micStopFlag) {
                val n = record.read(buf, 0, buf.size)
                if (n > 0) {
                    client?.sendMicAudio(sampleRate, if (n == buf.size) buf else buf.copyOf(n))
                } else if (n < 0) {
                    break // AudioRecord.ERROR_* -- give up rather than spin.
                }
            }
        }
        thread.isDaemon = true
        micThread = thread
        thread.start()
    }

    private fun stopMicCapture() {
        micStopFlag = true
        // Stop the record BEFORE joining -- AudioRecord.stop() is safe to
        // call from a different thread than the one blocked in read(), and
        // unblocks that read() promptly. Without this, join() has to wait
        // for whatever's already in flight in the capture thread's blocking
        // read() call to complete naturally (up to one buffer's worth,
        // tens of ms) -- onMicEnable() runs synchronously on the native
        // network thread that also drives video/audio/input polling, so
        // that wait was a small but avoidable hitch on every mic-disable
        // transition.
        micRecord?.let {
            if (it.recordingState == AudioRecord.RECORDSTATE_RECORDING) {
                it.stop()
            }
        }
        micThread?.join(500)
        micThread = null
        micRecord?.release()
        micRecord = null
    }

    override fun onDisconnected(reason: String) {
        // The native session thread calls this right before it exits on its
        // own (connect/handshake failure, peer closed, protocol error) --
        // must still route through disconnect() to join the thread and
        // release the global JNI ref to this Activity, or both leak. A
        // user-initiated exit goes through the system back button instead,
        // straight to onDestroy(), never through here -- so this is always
        // an unexpected drop.
        runOnUiThread {
            val wasConnected = connected
            disconnect()
            connected = false
            if (wasConnected) {
                // Stream was up and dropped on its own -- the hidden status
                // text alone wouldn't be noticed mid-game, so surface it.
                disconnectedReason = reason
            } else {
                // Never got connected in the first place (e.g. handshake
                // failure) -- no stream was "lost", just show it inline.
                statusText = getString(R.string.status_error, reason)
            }
        }
    }

    override fun onDestroy() {
        stopMicCapture()
        disconnect()
        super.onDestroy()
    }

    companion object {
        const val EXTRA_HOST = "host"
        const val EXTRA_STREAM_TYPE = "stream_type"
        const val EXTRA_PORT = "port"
    }
}
