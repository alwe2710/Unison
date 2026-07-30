package com.finlink.android

/**
 * Thin Kotlin shell around the native session: connects, decodes, and sends
 * input entirely in C (jni_bridge.c + finlink_core). This class only owns
 * the JNI handle and forwards calls; there's no protocol/transport logic on
 * the JVM side at all.
 */
class GbaStreamClient(private val listener: Listener) {

    interface Listener {
        // isTouch/hasButtons/hasSticks reflect the server's own
        // hello.input_encoding ("n3ds_touch_and_buttons" vs
        // "touch_and_buttons" vs "n3ds_touch" vs "gba_buttons", see
        // jni_bridge.c's perform_app_handshake), not a client-side guess
        // from the discovery beacon -- authoritative even for manual host
        // entry (which has no beacon to read a stream_type from beforehand)
        // and across a redirect hop landing on a different stream type than
        // the one first dialed. hasButtons is only ever true alongside
        // isTouch (there's no buttons-without-touch encoding); it's false
        // for "n3ds_touch" servers that don't accept remote buttons at all
        // (Cemu), true for both "touch_and_buttons" (melonDS's
        // NDS_BOTTOM_SCREEN -- buttons, but no analog sticks, the DS has
        // none on real hardware) and "n3ds_touch_and_buttons" (Azahar's
        // N3DS_BOTTOM_SCREEN -- buttons AND circle pad/analog sticks).
        // hasSticks distinguishes those last two; only ever true alongside
        // hasButtons.
        fun onConnected(isTouch: Boolean, hasButtons: Boolean, hasSticks: Boolean)
        fun onVideoFrame(width: Int, height: Int, rgb565: ByteArray)
        fun onAudioFrame(sampleRate: Int, channels: Int, pcm: ShortArray)
        // The server's own on-screen software keyboard (e.g. Cemu's swkbd)
        // is drawn as a host-side UI overlay, never part of the captured
        // video -- this is the server asking the client to show its own
        // native text input UI instead, since there'd otherwise be no way
        // to type at all. maxLength is in characters, 0 = no server-side
        // limit; initialText is whatever's already in the field (often
        // empty). Reply with sendTextInputResponse() once the user submits
        // or cancels.
        fun onTextInputRequest(maxLength: Int, initialText: String)
        // Mirrors real mic hardware: the console only wants microphone
        // input while a game has it powered on and actively sampling (see
        // e.g. the 3DS's mic:u service or the Wii U GamePad's own mic
        // input), not continuously just because a stream is connected --
        // start (or stop) capturing from the device's own microphone via
        // sendMicAudio() only while enabled is true, at sampleRate (the
        // exact rate the console asked for; AudioRecord accepts arbitrary
        // rates and resamples internally, so this can be requested
        // directly without the client doing its own resampling). Requires
        // RECORD_AUDIO at runtime -- if the permission isn't granted, the
        // implementation should simply not capture rather than crash.
        fun onMicEnable(enabled: Boolean, sampleRate: Int)
        fun onDisconnected(reason: String)
    }

    @Volatile
    private var nativeHandle: Long = 0

    /** Spawns a background native thread; connect result arrives via onConnected/onDisconnected.
     * videoMode is sent verbatim as hello_ack.video_mode (Prefs.videoMode, one of Prefs.VIDEO_MODES) --
     * see docs/protocol.md; servers that don't implement the negotiation just ignore it. */
    fun connect(host: String, port: Int, videoMode: String = Prefs.VIDEO_MODE_DEFAULT) {
        nativeHandle = nativeConnect(host, port, videoMode, listener)
    }

    /** Cheap: just records the latest key state, the native session loop sends it.
     * Only meaningful for a gba_buttons session (Listener.onConnected(isTouch = false)). */
    fun sendInput(keyMask: Int) {
        val handle = nativeHandle
        if (handle != 0L) nativeSendInput(handle, keyMask)
    }

    /** Touch counterpart to sendInput -- only meaningful for a touch session
     * (Listener.onConnected(isTouch = true)). x/y are in the current video
     * frame's own native pixel coordinates (see PlayerActivity's touch
     * overlay for the tap-position mapping); ignored by the receiver
     * whenever pressed is false (finlink_touch_state's own convention,
     * protocol.h), so any value is fine there -- 0 is simplest. */
    fun sendTouch(pressed: Boolean, x: Int, y: Int) {
        val handle = nativeHandle
        if (handle != 0L) nativeSendTouch(handle, pressed, x, y)
    }

    /** Combined counterpart to sendTouch -- only meaningful for a session
     * where Listener.onConnected reported hasButtons = true. buttons is an
     * OR of the BUTTON_* constants below; leftX/leftY is the circle pad or,
     * on a two-stick console, the left stick (-32768..32767 per axis, 0
     * centered); rightX/rightY is always 0 from a caller with only one
     * stick to report. touchPressed/touchX/touchY follow sendTouch's own
     * convention (x/y ignored by the receiver whenever touchPressed is
     * false). Stick args are meaningless (and simply dropped native-side,
     * see jni_bridge.c's maybe_send_touch()) when hasSticks = false --
     * callers there (no VirtualStick UI shown at all) should just always
     * pass 0. */
    fun sendExtendedInput(
        touchPressed: Boolean, touchX: Int, touchY: Int,
        buttons: Int, leftX: Int, leftY: Int, rightX: Int = 0, rightY: Int = 0
    ) {
        val handle = nativeHandle
        if (handle != 0L) {
            nativeSendExtendedInput(handle, touchPressed, touchX, touchY, buttons, leftX, leftY, rightX, rightY)
        }
    }

    /** Reply to Listener.onTextInputRequest() -- confirmed=false (user
     * cancelled) sends regardless of what's in text, the server is expected
     * to leave its existing text unchanged in that case (finlink's own
     * finlink_text_input_response convention). */
    fun sendTextInputResponse(confirmed: Boolean, text: String) {
        val handle = nativeHandle
        if (handle != 0L) nativeSendTextInputResponse(handle, confirmed, text)
    }

    /** Uploads a chunk of mono s16 PCM samples captured from the device's own
     * microphone -- only meaningful while Listener.onMicEnable(true, ...)
     * is the most recent state; harmless (silently queued, just never
     * useful) otherwise. sampleRate should match whatever onMicEnable most
     * recently reported. */
    fun sendMicAudio(sampleRate: Int, samples: ShortArray) {
        val handle = nativeHandle
        if (handle != 0L) nativeSendMicAudio(handle, sampleRate, samples)
    }

    fun disconnect() {
        val handle = nativeHandle
        if (handle != 0L) {
            nativeHandle = 0
            nativeDisconnect(handle)
        }
    }

    private external fun nativeConnect(host: String, port: Int, videoMode: String, listener: Listener): Long
    private external fun nativeSendInput(handle: Long, keyMask: Int)
    private external fun nativeSendTouch(handle: Long, pressed: Boolean, x: Int, y: Int)
    private external fun nativeSendExtendedInput(
        handle: Long, touchPressed: Boolean, touchX: Int, touchY: Int,
        buttons: Int, leftX: Int, leftY: Int, rightX: Int, rightY: Int
    )
    private external fun nativeSendTextInputResponse(handle: Long, confirmed: Boolean, text: String)
    private external fun nativeSendMicAudio(handle: Long, sampleRate: Int, samples: ShortArray)
    private external fun nativeDisconnect(handle: Long)

    companion object {
        // Player ports 6801-6804 (docs/protocol.md); /status on each one
        // (not a combined endpoint) is still how the P1-P4 picker below
        // finds a free slot -- the app-level handshake (core's
        // finlink/handshake.h, spoken over the WebSocket connection itself
        // once nativeConnect() dials a specific port) is a separate,
        // later step, not a replacement for this pre-connect check.
        const val PLAYER_BASE_PORT = 6801
        const val PLAYER_SLOT_COUNT = 4

        // The one stream type with a lobby port fanning out to separate
        // per-slot ports (see the P1-P4 picker above) -- every other stream
        // type (N3DS_BOTTOM_SCREEN, NDS_BOTTOM_SCREEN, WIIU_GAMEPAD, ...) is
        // single-client and its beacon's handshake_port *is* the only port
        // there is, so MenuActivity connects to it directly instead of
        // running the GC_GBA_LINK-specific slot probe against it (which
        // used to always report "alle Plätze belegt" for these -- ports
        // 6801-6804 never existed on a server that isn't Dolphin).
        const val STREAM_TYPE_GC_GBA_LINK = "GC_GBA_LINK"

        // Mirrors FINLINK_PROTOCOL_VERSION (core/include/finlink/handshake.h)
        // and FINLINK_BEACON_PORT (core/include/finlink/discovery.h) --
        // MenuActivity's discovery listener needs both before any native
        // handshake code runs, so it can't just call into core for them.
        const val PROTOCOL_VERSION = 2
        const val BEACON_PORT = 6805

        // Mirrors finlink_key in core/include/finlink/protocol.h.
        const val KEY_A = 1 shl 0
        const val KEY_B = 1 shl 1
        const val KEY_SELECT = 1 shl 2
        const val KEY_START = 1 shl 3
        const val KEY_RIGHT = 1 shl 4
        const val KEY_LEFT = 1 shl 5
        const val KEY_UP = 1 shl 6
        const val KEY_DOWN = 1 shl 7
        const val KEY_R = 1 shl 8
        const val KEY_L = 1 shl 9

        // Mirrors finlink_button_bit in core/include/finlink/protocol.h --
        // for sendExtendedInput's buttons parameter, not sendInput's
        // keyMask (KEY_* above, an unrelated bit layout for gba_buttons).
        // A given bit only means something to a server whose console
        // actually has that button; see finlink_button_bit's own comment.
        const val BUTTON_A = 1 shl 0
        const val BUTTON_B = 1 shl 1
        const val BUTTON_X = 1 shl 2
        const val BUTTON_Y = 1 shl 3
        const val BUTTON_L = 1 shl 4
        const val BUTTON_R = 1 shl 5
        const val BUTTON_ZL = 1 shl 6
        const val BUTTON_ZR = 1 shl 7
        const val BUTTON_SELECT = 1 shl 8 // aka Minus (Wii U)
        const val BUTTON_START = 1 shl 9  // aka Plus (Wii U)
        const val BUTTON_UP = 1 shl 10
        const val BUTTON_DOWN = 1 shl 11
        const val BUTTON_LEFT = 1 shl 12
        const val BUTTON_RIGHT = 1 shl 13
        const val BUTTON_HOME = 1 shl 14

        init {
            System.loadLibrary("finlink_android")
        }
    }
}
