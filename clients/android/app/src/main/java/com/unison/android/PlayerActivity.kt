package com.unison.android

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Bundle
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
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

    // Held for as long as the stream is being watched (acquired in
    // onCreate, released in onDestroy, same lifetime as
    // FLAG_KEEP_SCREEN_ON above) -- Android's Wi-Fi power-save mode
    // periodically batches incoming packets even for an app in the
    // foreground with an active connection, which reads as exactly the
    // "nothing for a few hundred ms, then several frames' worth of data
    // arrives at once" pattern this session's own decode-backlog
    // diagnostic kept showing, unaffected by any server-side encoder
    // change (see WiiuGamepadStream.cpp's rate-control history) -- a
    // strong sign the batching was happening on this end, not the
    // server's. WIFI_MODE_FULL_LOW_LATENCY (Android 10+) is the mode
    // built for exactly this (real-time audio/video); older versions fall
    // back to WIFI_MODE_FULL_HIGH_PERF, which at minimum disables power-save
    // polling even if it doesn't tune latency as tightly.
    private var wifiLock: WifiManager.WifiLock? = null

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

    // Launched from GbaStreamClient.Listener.onTextInputRequest() (the
    // server's own on-screen keyboard has no way to reach a remote client,
    // see that callback's own comment) -- a real Activity, not a Dialog on
    // top of this one, so the system keyboard's own fullscreen input isn't
    // fighting a second app-drawn window (see TextInputActivity's own
    // comment for why that mattered).
    private val textInputLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            val text = result.data?.getStringExtra(TextInputActivity.EXTRA_RESULT_TEXT) ?: ""
            client?.sendTextInputResponse(true, text)
        } else {
            client?.sendTextInputResponse(false, "")
        }
    }

    // Set from GbaStreamClient.Listener.onConnected(isTouch, hasButtons),
    // i.e. from the server's own hello.input_encoding -- not known before
    // that point, so both stay false (the GBA button overlay below) until
    // then regardless of what kind of server was actually dialed. See
    // TouchOverlay/ExtendedControlsOverlay for what each switches to.
    private var touchMode by mutableStateOf(false)
    private var hasButtonsMode by mutableStateOf(false)
    // session_ready.video's final resolution (GbaStreamClient.Listener.
    // onConnected) -- known regardless of negotiated video_mode, unlike
    // videoBitmap's own width/height (only ever set for tiles/legacy, see
    // onVideoFrame's comment below). TouchOverlay uses these instead of
    // videoBitmap?.width/height so touch input works the same way for an
    // h264/h265 session, where videoBitmap is never populated at all.
    private var streamWidth by mutableStateOf(0)
    private var streamHeight by mutableStateOf(0)
    // Non-null exactly when onConnected() saw a genuine requested-vs-granted
    // mismatch (docs/protocol.md "Video-mode fallback") -- (requested,
    // granted) wire-format mode strings, mapped to their label resources
    // where shown. The stream is already live/rendering underneath this by
    // the time it's shown (session_ready already committed the server to
    // `granted`), so this is a non-blocking heads-up, not a gate on
    // playback starting -- "Fortsetzen" just dismisses it.
    private var videoModeFallback by mutableStateOf<Pair<String, String>?>(null)
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
    // unison_extended_input frame per change (GbaStreamClient.
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
    // A real physical controller's analog thumbsticks -- fed from
    // onGenericMotionEvent, see that override's own comment. Always drives
    // the session's own sticks directly, same as VirtualStick's touch-drag
    // contribution and unlike a bindable digital button -- a real
    // thumbstick has no equivalent concept of "unbound" the way a keyboard
    // key does (mirrors clients/ios's own ControllerInputHandler, whose
    // comment states this exact convention outright).
    private var extStickLPhysicalX = 0
    private var extStickLPhysicalY = 0
    private var extStickRPhysicalX = 0
    private var extStickRPhysicalY = 0
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
        val leftX = (extStickLDragX + extStickLPhysicalX + axis(extKeyStickLLeft, extKeyStickLRight)).coerceIn(-32768, 32767)
        val leftY = (extStickLDragY + extStickLPhysicalY + axis(extKeyStickLDown, extKeyStickLUp)).coerceIn(-32768, 32767)
        val rightX = (extStickRDragX + extStickRPhysicalX + axis(extKeyStickRLeft, extKeyStickRRight)).coerceIn(-32768, 32767)
        val rightY = (extStickRDragY + extStickRPhysicalY + axis(extKeyStickRDown, extKeyStickRUp)).coerceIn(-32768, 32767)
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

        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
        val lockMode = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
            WifiManager.WIFI_MODE_FULL_LOW_LATENCY
        else
            @Suppress("DEPRECATION") WifiManager.WIFI_MODE_FULL_HIGH_PERF
        wifiLock = wifiManager.createWifiLock(lockMode, "unison-stream").apply { acquire() }

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
            UnisonTheme {
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
                // Bottom-most layer: MediaCodec's Surface-mode H.264/H265
                // output composites directly into this SurfaceView's
                // Surface (see GbaStreamClient.setVideoSurface(),
                // jni_bridge.c's ensure_video_codec()) -- no CPU pixel copy,
                // and no onVideoFrame callback at all for that path. Always
                // present (regardless of negotiated video_mode): for
                // tiles/legacy, nothing ever renders into it and the Image
                // below simply covers it; for h264/h265, videoBitmap is
                // never set (see onVideoFrame's own comment) so the Image
                // block below never emits, letting this show through
                // instead. Either way there's no need to track which mode
                // is actually active here.
                VideoSurfaceView(modifier = Modifier.fillMaxSize())

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
                }

                // Touch mode's only input method -- not gated on
                // onScreenControlsEnabled (that preference is about the
                // optional GBA button overlay below; a touch-based stream
                // has no other way to provide input at all, so there's
                // nothing to make optional here). Uses streamWidth/Height
                // (session_ready.video, known at connect time) rather than
                // videoBitmap's own dimensions, since videoBitmap is never
                // set at all for an h264/h265 session -- see
                // streamWidth/streamHeight's own comment.
                if (touchMode && streamWidth > 0 && streamHeight > 0) {
                    TouchOverlay(streamWidth, streamHeight, modifier = Modifier.fillMaxSize())
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
                // unison_extended_input/unison_touch_and_buttons frame,
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

            // Non-blocking: the stream is already live in the granted mode
            // by the time this can even show (see videoModeFallback's own
            // comment) -- "Fortsetzen" just dismisses it, "Abbrechen" tears
            // down the session same as a normal user-initiated disconnect.
            videoModeFallback?.let { (requested, granted) ->
                Dialog(onDismissRequest = { videoModeFallback = null }) {
                    Surface(shape = RoundedCornerShape(16.dp), color = MaterialTheme.colorScheme.surface) {
                        Column(modifier = Modifier.padding(20.dp)) {
                            Text(stringResource(R.string.video_mode_fallback_title), style = MaterialTheme.typography.titleMedium)
                            Spacer(Modifier.height(8.dp))
                            Text(
                                stringResource(
                                    R.string.video_mode_fallback_message,
                                    stringResource(videoModeLabelRes(requested)),
                                    stringResource(videoModeLabelRes(granted))
                                ),
                                style = MaterialTheme.typography.bodyMedium
                            )
                            Spacer(Modifier.height(12.dp))
                            Row(modifier = Modifier.align(Alignment.End)) {
                                TextButton(onClick = {
                                    videoModeFallback = null
                                    disconnect()
                                    finish()
                                }) {
                                    Text(stringResource(R.string.video_mode_fallback_abort))
                                }
                                TextButton(onClick = { videoModeFallback = null }) {
                                    Text(stringResource(R.string.video_mode_fallback_continue))
                                }
                            }
                        }
                    }
                }
            }

        }
    }

    /** Wire-format video_mode string ("tiles"/"legacy"/"h264"/"h265") ->
     * its user-facing label resource, reusing Prefs.VIDEO_MODES (the same
     * source VideoModeActivity's picker already uses) instead of a second,
     * separately-maintained mapping. Falls back to R.string.video_mode_tiles
     * only as a last resort -- every value either side of this feature can
     * legitimately send is already one of Prefs.VIDEO_MODES' entries. */
    private fun videoModeLabelRes(mode: String): Int =
        Prefs.VIDEO_MODES.find { it.value == mode }?.labelRes ?: R.string.video_mode_tiles

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
    /** Backs the H.264/H265 render target -- see PlayerScreen's own comment
     * on why this is always present regardless of negotiated video_mode.
     *
     * SurfaceView, not TextureView: the identical bitstream (same encoder,
     * same server) rendered correctly through TextureView on the Android
     * emulator's software decoder, but showed persistent visible
     * distortion on a real device's hardware decoder for both H.264 and
     * H.265 alike -- unaffected by every server-side change tried (bitrate
     * mode, VBV sizing, H.264 profile), which points at the shared
     * TextureView GL-texture-compositing path itself as the actual
     * culprit, a known category of real-device-specific bug distinct from
     * TextureView's normal Compose-friendly behavior. SurfaceView instead
     * gets its own dedicated hardware compositor layer, bypassing GL
     * texture compositing entirely. setZOrderMediaOverlay(true) keeps it
     * above the Activity's own window background but still below the
     * touch/button overlay Composables drawn in the same window on top of
     * it in PlayerScreen -- unlike setZOrderOnTop(true), which would put
     * it above the whole window, including those overlays. */
    @Composable
    private fun VideoSurfaceView(modifier: Modifier = Modifier) {
        AndroidView(
            modifier = modifier,
            factory = { context ->
                SurfaceView(context).apply {
                    setZOrderMediaOverlay(true)
                    holder.addCallback(object : SurfaceHolder.Callback {
                        override fun surfaceCreated(holder: SurfaceHolder) {
                            client?.setVideoSurface(holder.surface)
                        }

                        override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {}

                        override fun surfaceDestroyed(holder: SurfaceHolder) {
                            client?.setVideoSurface(null)
                        }
                    })
                }
            }
        )
    }

    /** The whole visible video area doubles as the touch surface: press,
     * drag, and release all map 1:1 to unison_touch_state's pressed/x/y
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
    //
    // dispatchKeyEvent, not onKeyDown/onKeyUp: reported live -- a bound
    // controller press always got ALSO picked up as ordinary system input
    // at the same time (on-screen focus visibly jumping between the
    // Compose touch controls, sometimes a highlighted control's own click
    // firing) alongside actually reaching the game correctly. Root cause:
    // onKeyDown/onKeyUp are only a *fallback* Android calls if nothing
    // earlier in dispatch already consumed the event -- and a gamepad's
    // D-pad is delivered as the exact same KEYCODE_DPAD_UP/DOWN/LEFT/RIGHT
    // (plus KEYCODE_DPAD_CENTER/KEYCODE_BUTTON_A-as-"click") codes Android
    // uses for its own built-in focus-navigation-and-click handling on any
    // focusable View/composable -- which runs *before* onKeyDown/onKeyUp
    // ever gets a look in, in the DecorView's own dispatch, not after.
    // Overriding dispatchKeyEvent (same mechanism KeyBindingsActivity's own
    // binding-capture already relies on, see that Activity's own comment)
    // intercepts the event at the very first possible point, before any
    // View/composable's default focus-navigation gets a chance to react to
    // it at all -- the same fix every other Android game/emulator project
    // uses for this exact, well-known problem (RetroArch, Dolphin Android,
    // Yuzu, DraStic, ...).
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        // TEMPORARY diagnostic (2026-08-06) -- two real fix attempts for
        // controller input still didn't resolve it on real hardware;
        // rather than guess a third time, log every single event this
        // override actually sees. Remove once the real cause is found.
        android.util.Log.d("UnisonInputDiag", "dispatchKeyEvent keyCode=${event.keyCode} " +
            "action=${event.action} source=0x${event.source.toString(16)} " +
            "device=${event.device?.name} bound=${keyCodeToBit.containsKey(event.keyCode)} " +
            "hasButtonsMode=$hasButtonsMode")
        when (event.action) {
            KeyEvent.ACTION_DOWN -> {
                if (hasButtonsMode && handleExtKey(event.keyCode, pressed = true)) return true
                val bit = keyCodeToBit[event.keyCode] ?: return super.dispatchKeyEvent(event)
                physicalMask = physicalMask or bit
                sendCombinedInput()
                return true
            }
            KeyEvent.ACTION_UP -> {
                if (hasButtonsMode && handleExtKey(event.keyCode, pressed = false)) return true
                val bit = keyCodeToBit[event.keyCode] ?: return super.dispatchKeyEvent(event)
                physicalMask = physicalMask and bit.inv()
                sendCombinedInput()
                return true
            }
        }
        return super.dispatchKeyEvent(event)
    }

    /** Real analog thumbstick input -- reported live: the right stick was
     * never recognized at all, the left only ever registered as D-pad
     * presses. Root cause, confirmed by there being no generic-motion
     * handling anywhere in this class before this fix: a physical
     * thumbstick reports through MotionEvent axis values (AXIS_X/AXIS_Y
     * for the left stick, AXIS_Z/AXIS_RZ for the right -- Android's own
     * documented standard gamepad axis mapping), never as a KeyEvent at
     * all, so dispatchKeyEvent above could never see it regardless of any
     * fix there -- an entirely separate Android input path needs its own
     * separate handling. The left stick's own "only ever seen as D-pad"
     * symptom specifically is a controller/driver-level quirk, not
     * something fixable from here: some gamepads report their D-pad (a hat
     * switch) on the very same underlying HID report field their left
     * stick would otherwise share, which Android additionally synthesizes
     * into KEYCODE_DPAD_* automatically -- reading AXIS_X/AXIS_Y directly
     * here (rather than relying on any DPAD KeyEvent) is the most this app
     * itself can do; if the controller's own firmware/driver genuinely
     * never exposes a distinct analog axis for that stick, no app-level
     * fix can recover data the OS was never given.
     *
     * dispatchGenericMotionEvent, not onGenericMotionEvent -- the exact
     * same fallback-vs-first-look mistake dispatchKeyEvent (above) already
     * fixed for KeyEvents, just for MotionEvent's own separate dispatch
     * chain: onGenericMotionEvent is only Android's *fallback*, called
     * after the DecorView/Compose hierarchy's own dispatch already had
     * first crack -- any focusable/pointer-input composable on screen
     * (the on-screen ExtHoldButton/VirtualStick controls themselves
     * included) consuming the event there means onGenericMotionEvent
     * never gets called at all. First real live test after adding
     * onGenericMotionEvent confirmed this the hard way: neither stick
     * produced any input whatsoever, worse than before the fix existed at
     * all, not better.
     *
     * Deadzone: matches VirtualStick's own already-live analog precedent
     * only loosely (that one has no deadzone at all, being a bounded touch
     * drag with no physical center-rest drift) -- 0.15 is the standard,
     * widely-used starting point for a real analog stick's own resting
     * noise (Android's own official game controller sample uses the same
     * value), not levels this codebase already had, since none of the
     * other physical-input paths (keyboard digital-stick, VirtualStick's
     * own touch drag) have any physical rest-position noise to filter. */
    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        if (event.action != MotionEvent.ACTION_MOVE ||
            (event.source and InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK
        ) {
            return super.dispatchGenericMotionEvent(event)
        }

        // D-pad-via-hat-axis, independent of hasSticksMode (unlike the
        // stick reading below) -- a D-pad exists regardless of whether this
        // session negotiated real analog sticks. See handleHatAxes' own
        // comment for why this exists at all: confirmed live (DualSense)
        // that some controllers report their D-pad *only* this way, no
        // KeyEvent ever generated for it, so dispatchKeyEvent above alone
        // can never cover every controller's D-pad.
        handleHatAxes(event.getAxisValue(MotionEvent.AXIS_HAT_X), event.getAxisValue(MotionEvent.AXIS_HAT_Y))

        if (!hasSticksMode) {
            return true
        }

        fun axis(code: Int): Float {
            val v = event.getAxisValue(code)
            return if (kotlin.math.abs(v) < STICK_AXIS_DEADZONE) 0f else v
        }

        // Y sign matches VirtualStick's own convention (see its comment):
        // positive = stick pushed up, opposite of MotionEvent's own raw
        // AXIS_Y/AXIS_RZ (positive = pushed down/away).
        val newLX = (axis(MotionEvent.AXIS_X) * 32767f).toInt()
        val newLY = (-axis(MotionEvent.AXIS_Y) * 32767f).toInt()
        val newRX = (axis(MotionEvent.AXIS_Z) * 32767f).toInt()
        val newRY = (-axis(MotionEvent.AXIS_RZ) * 32767f).toInt()
        // TEMPORARY diagnostic (2026-08-06) -- only on an actual change,
        // not this controller's continuous at-rest ACTION_MOVE stream.
        if (newLX != extStickLPhysicalX || newLY != extStickLPhysicalY ||
            newRX != extStickRPhysicalX || newRY != extStickRPhysicalY
        ) {
            android.util.Log.d("UnisonInputDiag",
                "stick change L=($newLX,$newLY) R=($newRX,$newRY) rawX=${event.getAxisValue(MotionEvent.AXIS_X)} " +
                    "rawY=${event.getAxisValue(MotionEvent.AXIS_Y)} rawZ=${event.getAxisValue(MotionEvent.AXIS_Z)} " +
                    "rawRZ=${event.getAxisValue(MotionEvent.AXIS_RZ)}")
        }
        extStickLPhysicalX = newLX
        extStickLPhysicalY = newLY
        extStickRPhysicalX = newRX
        extStickRPhysicalY = newRY
        sendCombinedExtendedInput()
        return true
    }

    // Which of the four synthetic KEYCODE_DPAD_* directions handleHatAxes()
    // currently considers "held", so it only reacts to an actual state
    // change rather than resending a press on every single motion tick --
    // a real hat switch's own controller keeps streaming ACTION_MOVE
    // continuously even while sitting still at rest (confirmed live,
    // DualSense), same "confirmed live" reasoning as the field below.
    private var hatDpadState = 0

    /** Synthesizes KEYCODE_DPAD_UP/DOWN/LEFT/RIGHT digital presses/releases
     * from a hat-switch axis and routes them through the exact same
     * keyCodeToBit/handleExtKey path dispatchKeyEvent's own real KeyEvent
     * case uses -- confirmed live (DualSense, real logcat capture) that a
     * D-pad press can produce this axis and NO KeyEvent at all, so
     * dispatchKeyEvent by itself can never see or handle that controller's
     * D-pad regardless of any fix made there; this is the actual missing
     * half. KeyBindingsActivity's own dispatchGenericMotionEvent mirrors
     * this same KEYCODE_DPAD_* identity when *capturing* a binding for a
     * hat-only D-pad, so a button bound that way round-trips correctly
     * through keyCodeToBit here (built from that exact same stored value)
     * without this class needing to know or care which of the two paths a
     * given controller's D-pad actually used. */
    private fun handleHatAxes(hatX: Float, hatY: Float) {
        val newState = (if (hatX < -0.5f) 1 else 0) or
            (if (hatX > 0.5f) 2 else 0) or
            (if (hatY < -0.5f) 4 else 0) or
            (if (hatY > 0.5f) 8 else 0)
        if (newState == hatDpadState) return
        val changed = newState xor hatDpadState

        fun apply(bitFlag: Int, keyCode: Int) {
            if (changed and bitFlag == 0) return
            val pressed = newState and bitFlag != 0
            if (hasButtonsMode && handleExtKey(keyCode, pressed = pressed)) return
            val bit = keyCodeToBit[keyCode] ?: return
            physicalMask = if (pressed) physicalMask or bit else physicalMask and bit.inv()
            sendCombinedInput()
        }
        apply(1, KeyEvent.KEYCODE_DPAD_LEFT)
        apply(2, KeyEvent.KEYCODE_DPAD_RIGHT)
        apply(4, KeyEvent.KEYCODE_DPAD_UP)
        apply(8, KeyEvent.KEYCODE_DPAD_DOWN)
        hatDpadState = newState
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
        extStickLPhysicalX = 0
        extStickLPhysicalY = 0
        extStickRPhysicalX = 0
        extStickRPhysicalY = 0
        hatDpadState = 0
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
        c.connect(host, port, prefs.videoMode)
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

    override fun onConnected(isTouch: Boolean, hasButtons: Boolean, hasSticks: Boolean, width: Int, height: Int, grantedVideoMode: String) {
        // TEMPORARY diagnostic (2026-08-06) -- reported live: sticks still
        // not read even after dispatchGenericMotionEvent started correctly
        // firing (confirmed via the D-pad fix working). One cheap
        // possibility to rule in/out first: hasSticksMode itself simply
        // being false for whichever server this session was tested
        // against (dispatchGenericMotionEvent's own stick-reading branch
        // is unconditionally gated on it) -- not every stream type reports
        // real analog sticks (melonDS/NDS_BOTTOM_SCREEN has none, the
        // hardware itself never had any; GC_GBA_LINK has no touch/sticks
        // at all either).
        android.util.Log.d("UnisonInputDiag",
            "onConnected isTouch=$isTouch hasButtons=$hasButtons hasSticks=$hasSticks")
        runOnUiThread {
            connected = true
            touchMode = isTouch
            hasButtonsMode = hasButtons
            hasSticksMode = hasSticks
            streamWidth = width
            streamHeight = height
            // Blank grantedVideoMode means the server predates
            // session_ready.video_mode entirely -- skip the comparison
            // rather than assuming "tiles" was granted, see
            // docs/protocol.md "Video-mode fallback" and this field's own
            // comment in GbaStreamClient.Listener.
            val requested = prefs.videoMode
            if (grantedVideoMode.isNotBlank() && requested != grantedVideoMode) {
                videoModeFallback = requested to grantedVideoMode
            }
        }
    }

    override fun onTextInputRequest(maxLength: Int, initialText: String) {
        runOnUiThread {
            textInputLauncher.launch(
                Intent(this, TextInputActivity::class.java).apply {
                    putExtra(TextInputActivity.EXTRA_MAX_LENGTH, maxLength)
                    putExtra(TextInputActivity.EXTRA_INITIAL_TEXT, initialText)
                }
            )
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
        if (wifiLock?.isHeld == true) wifiLock?.release()
        super.onDestroy()
    }

    companion object {
        const val EXTRA_HOST = "host"
        const val EXTRA_STREAM_TYPE = "stream_type"
        const val EXTRA_PORT = "port"
        // See onGenericMotionEvent's own comment.
        private const val STICK_AXIS_DEADZONE = 0.15f
    }
}
