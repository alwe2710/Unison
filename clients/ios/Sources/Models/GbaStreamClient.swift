import Foundation

/// Thin Swift shell around the native session: connects, decodes, and
/// sends input entirely in C (unison_native_bridge.c + unison_core). This
/// class only owns the native handle and forwards calls -- direct port of
/// clients/android/.../GbaStreamClient.kt, with unison_native_bridge.c's
/// C function-pointer callbacks in place of JNI (see that file's own
/// comment for the full "no JNI-equivalent glue needed" rationale).
///
/// Like the Android original, Listener callbacks fire on the native
/// background thread, not the main thread -- same contract, same reason
/// (this class stays a thin, allocation-free-as-possible shell; hopping to
/// the main thread for UI updates is the caller's job, same as
/// PlayerActivity's own dispatch).
final class GbaStreamClient {
    protocol Listener: AnyObject {
        /// touchInput/hasButtons/hasSticks mirror GbaStreamClient.kt's
        /// Listener.onConnected -- see that interface's own extensive
        /// comment for the full input-encoding rationale. This MVP bridge
        /// only ever actually negotiates a gba_buttons (touchInput=false)
        /// session; the other fields exist for parity with a future
        /// touch-input PlayerView.
        func onConnected(touchInput: Bool, hasButtons: Bool, hasSticks: Bool, width: Int32, height: Int32,
                          grantedVideoMode: String)
        /// rgb565 is a fresh Data copy (unlike the C callback's raw
        /// pointer, which is only valid for that call) -- same "safe to
        /// keep" contract as Android's ByteArray.
        func onVideoFrame(width: Int32, height: Int32, rgb565: Data)
        /// pcm is a fresh [Int16] copy, same reasoning as rgb565 above.
        func onAudioFrame(sampleRate: Int32, channels: Int32, pcm: [Int16])
        func onDisconnected(reason: String)
    }

    static let videoModeDefault = Prefs.videoModeDefault
    // Mirrors GbaStreamClient.kt's own companion-object constants.
    static let streamTypeGcGbaLink = "GC_GBA_LINK"
    static let playerBasePort: Int32 = 6801

    private weak var listener: Listener?
    private var handle: OpaquePointer?
    // Keeps self alive across the C callback boundary (Unmanaged.passUnretained
    // in connect() only borrows a reference, it doesn't retain) -- released
    // in disconnect() once unison_native_disconnect() has joined the
    // background thread and guaranteed no further callback will fire.
    private var selfRetain: Unmanaged<GbaStreamClient>?

    init(listener: Listener) {
        self.listener = listener
    }

    /// Spawns a background native thread; connect result arrives via
    /// listener.onConnected/onDisconnected. videoMode is sent verbatim as
    /// hello_ack.video_mode (Prefs.videoMode, one of Prefs.videoModes) --
    /// see docs/protocol.md; servers that don't implement the negotiation
    /// just ignore it.
    func connect(host: String, port: Int32, videoMode: String = GbaStreamClient.videoModeDefault) {
        let retained = Unmanaged.passRetained(self)
        selfRetain = retained

        var callbacks = unison_native_callbacks()
        callbacks.user_data = retained.toOpaque()
        callbacks.on_connected = { userData, touchInput, hasButtons, hasSticks, width, height, grantedVideoMode in
            guard let userData else { return }
            let client = Unmanaged<GbaStreamClient>.fromOpaque(userData).takeUnretainedValue()
            let mode = grantedVideoMode.map { String(cString: $0) } ?? ""
            client.listener?.onConnected(touchInput: touchInput != 0, hasButtons: hasButtons != 0,
                                          hasSticks: hasSticks != 0, width: width, height: height,
                                          grantedVideoMode: mode)
        }
        callbacks.on_video_frame = { userData, width, height, rgb565, len in
            guard let userData, let rgb565 else { return }
            let client = Unmanaged<GbaStreamClient>.fromOpaque(userData).takeUnretainedValue()
            let data = Data(bytes: rgb565, count: len)
            client.listener?.onVideoFrame(width: width, height: height, rgb565: data)
        }
        callbacks.on_audio_frame = { userData, sampleRate, channels, pcm, sampleCount in
            guard let userData, let pcm else { return }
            let client = Unmanaged<GbaStreamClient>.fromOpaque(userData).takeUnretainedValue()
            let buffer = UnsafeBufferPointer(start: pcm, count: sampleCount)
            client.listener?.onAudioFrame(sampleRate: sampleRate, channels: channels, pcm: Array(buffer))
        }
        callbacks.on_disconnected = { userData, reason in
            guard let userData else { return }
            let client = Unmanaged<GbaStreamClient>.fromOpaque(userData).takeUnretainedValue()
            let text = reason.map { String(cString: $0) } ?? ""
            client.listener?.onDisconnected(reason: text)
        }

        handle = unison_native_connect(host, port, videoMode, callbacks)
    }

    /// Sets the current gba_buttons keymask (GbaKey bits, OR'd together) --
    /// resent (only if changed) once per session-loop iteration, same
    /// "latest wins" contract as GbaStreamClient.kt's nativeSendInput.
    func sendInput(keymask: UInt16) {
        unison_native_send_input(handle, keymask)
    }

    /// Blocks until the background thread has stopped (unison_native_
    /// disconnect() pthread_joins) -- guarantees no further Listener
    /// callback fires after this returns, same as the Kotlin original.
    func disconnect() {
        unison_native_disconnect(handle)
        handle = nil
        selfRetain?.release()
        selfRetain = nil
    }
}
