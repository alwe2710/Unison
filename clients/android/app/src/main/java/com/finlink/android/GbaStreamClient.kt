package com.finlink.android

/**
 * Thin Kotlin shell around the native session: connects, decodes, and sends
 * input entirely in C (jni_bridge.c + finlink_core). This class only owns
 * the JNI handle and forwards calls; there's no protocol/transport logic on
 * the JVM side at all.
 */
class GbaStreamClient(private val listener: Listener) {

    interface Listener {
        fun onConnected()
        fun onVideoFrame(width: Int, height: Int, rgb565: ByteArray)
        fun onAudioFrame(sampleRate: Int, channels: Int, pcm: ShortArray)
        fun onDisconnected(reason: String)
    }

    @Volatile
    private var nativeHandle: Long = 0

    /** Spawns a background native thread; connect result arrives via onConnected/onDisconnected. */
    fun connect(host: String, port: Int) {
        nativeHandle = nativeConnect(host, port, listener)
    }

    /** Cheap: just records the latest key state, the native session loop sends it. */
    fun sendInput(keyMask: Int) {
        val handle = nativeHandle
        if (handle != 0L) nativeSendInput(handle, keyMask)
    }

    fun disconnect() {
        val handle = nativeHandle
        if (handle != 0L) {
            nativeHandle = 0
            nativeDisconnect(handle)
        }
    }

    private external fun nativeConnect(host: String, port: Int, listener: Listener): Long
    private external fun nativeSendInput(handle: Long, keyMask: Int)
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

        init {
            System.loadLibrary("finlink_android")
        }
    }
}
