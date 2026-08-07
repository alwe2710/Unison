import AVFoundation
import SwiftUI
import UIKit

/// Streaming screen -- direct-enough analog of PlayerActivity.kt. Renders
/// decoded RGB565 frames (or, for an h264/h265 session, hands raw NAL data
/// to CompressedVideoDecoder for VideoToolbox to decode+display directly),
/// plays PCM audio via AVAudioEngine, and sends input: plain gba_buttons
/// for GC_GBA_LINK, or touch (+ buttons + analog sticks, depending on what
/// the session actually negotiated) for Cemu/Azahar/melonDS. Still no mic
/// (see clients/ios/README.md's "Phasing").
final class PlayerViewModel: NSObject, ObservableObject, GbaStreamClient.Listener {
    enum Status: Equatable {
        case connecting
        case connected
        case disconnected(reason: String)
    }

    @Published private(set) var status: Status = .connecting
    @Published private(set) var currentFrame: UIImage?
    // Set from onConnected -- not known before that point, so all default
    // to the plain gba_buttons shape (matches PlayerActivity.kt's own
    // touchMode/hasButtonsMode/hasSticksMode defaults). streamWidth/Height
    // is session_ready.video's final resolution, known regardless of
    // negotiated video_mode -- TouchOverlay uses this instead of
    // currentFrame's own width/height so touch input still works for an
    // h264/h265 session once one of those is actually decoded (currentFrame
    // is never populated at all for those yet, see onVideoFrame below).
    @Published private(set) var touchInput = false
    @Published private(set) var hasButtons = false
    @Published private(set) var hasSticks = false
    @Published private(set) var streamWidth: Int32 = 0
    @Published private(set) var streamHeight: Int32 = 0
    // Set from onConnected -- see that method's own comment on why this
    // (not just touchInput/hasButtons/hasSticks) is what Prefs.bilinear(for:)
    // needs. Empty (Prefs.bilinear(for:) then falls through to
    // defaultBilinear's own "unknown stream type" branch) until a real
    // hello has arrived.
    @Published private(set) var streamType = ""
    // session_ready.video_mode verbatim, set from onConnected's
    // grantedVideoMode -- "h264"/"h265" is what PlayerView's body switches
    // render paths on (CompressedVideoView instead of the plain
    // Image(uiImage:) below, since currentFrame is never populated for
    // those, see onVideoFrame's own doc comment on the Listener protocol).
    @Published private(set) var grantedVideoMode = ""

    // Set once by CompressedVideoView.onLayerReady (see that view's own
    // comment) -- only relevant for an h264/h265 session. compressedVideoDecoder
    // is built as soon as both this and grantedVideoMode are known (in
    // practice always in that order, see setDisplayLayer's own comment),
    // so the very first frame that arrives after the layer's ready gets
    // decoded immediately rather than dropped waiting on a decoder that
    // would otherwise be built lazily one frame late.
    private var displayLayer: AVSampleBufferDisplayLayer?
    private var compressedVideoDecoder: CompressedVideoDecoder?

    private var client: GbaStreamClient?
    private var keymask: UInt16 = 0
    private let prefs = Prefs()

    // MARK: - Physical key/controller input
    //
    // Two independent input sources feed the exact same state below: a
    // connected hardware keyboard (PlayerKeyInputView, keyed by
    // UIKeyboardHIDUsage raw values, matching the bindings set in
    // KeyBindingsView -- see this class's handlePhysicalKey()) and a
    // connected game controller (ControllerInputHandler.swift,
    // GameController framework -- a completely separate API with no
    // HID-code overlap with the keyboard at all, unlike Android's unified
    // KeyEvent story covering both). No per-button rebinding UI for a
    // controller (unlike the keyboard's KeyBindingsView) -- a fixed,
    // sensible default mapping instead (ControllerInputHandler's own
    // comment), matching how every other client's own physical D-pad/
    // buttons already have one fixed mapping with no rebind screen either.
    private lazy var keyCodeToBit = prefs.keyBindingsByKeyCode()
    private lazy var extKeyCodeToButton = prefs.extKeyBindingsByKeyCode()
    private lazy var keyCodeToExtBitFromGba = prefs.sharedExtButtonBitsByKeyCode()
    private var controllerHandler: ControllerInputHandler?

    // Keyboard stick-direction bindings are digital (held/not), unlike a
    // real analog stick or a game controller's own thumbstick -- these
    // track each of the (up to) 8 bound keys' held state so opposite
    // directions held together cancel out instead of the later key
    // "winning", same convention as PlayerActivity.kt's own
    // extKeyStickLUp/... fields.
    private var extKeyStickLUp = false, extKeyStickLDown = false
    private var extKeyStickLLeft = false, extKeyStickLRight = false
    private var extKeyStickRUp = false, extKeyStickRDown = false
    private var extKeyStickRLeft = false, extKeyStickRRight = false

    // Extended-input (touchInput && hasButtons) state: touch, buttons, and
    // both analog sticks all merge into ONE unison_extended_input/
    // unison_touch_and_buttons frame per change (see GbaStreamClient.
    // sendExtendedInput's own comment) -- every source (on-screen taps,
    // the physical-key/controller handling above, touch drags below) that
    // changes any one of these calls sendCombined(), which resends all of
    // them together, not just its own piece. Direct port of
    // PlayerActivity.kt's own extTouchPressed/extButtons/... state.
    private var extTouchPressed = false
    private var extTouchX: UInt16 = 0
    private var extTouchY: UInt16 = 0
    private var extButtons: UInt32 = 0
    private var extLeftX: Int16 = 0
    private var extLeftY: Int16 = 0
    private var extRightX: Int16 = 0
    private var extRightY: Int16 = 0

    // Audio playback state -- only ever touched from GbaStreamClient's
    // callbacks, which (see unison_native_bridge.c's run_session_loop) all
    // fire serially from the same single background thread, never
    // concurrently with each other. That's what makes the lazy,
    // unsynchronized setup in ensureAudioEngine() below safe without an
    // explicit lock.
    private var audioEngine: AVAudioEngine?
    private var playerNode: AVAudioPlayerNode?
    private var audioFormat: AVAudioFormat?

    func connect(host: String, port: Int32, knownStreamType: String) {
        let client = GbaStreamClient(listener: self)
        self.client = client
        client.connect(host: host, port: port, videoMode: prefs.videoMode(for: knownStreamType))
        controllerHandler = ControllerInputHandler(viewModel: self)
    }

    func disconnect() {
        client?.disconnect()
        client = nil
        audioEngine?.stop()
        audioEngine = nil
        playerNode = nil
        audioFormat = nil
        controllerHandler = nil
        // Not displayLayer itself (CompressedVideoView's host UIView owns
        // it, torn down separately by SwiftUI once this view disappears) --
        // just this view model's own reference and the decoder built
        // against it, so a later reconnect doesn't reuse either.
        displayLayer = nil
        compressedVideoDecoder = nil
    }

    /// Called from PlayerView's button views (on-screen taps, a bound
    /// physical key, or a game controller's own fixed mapping -- see this
    /// class's own MARK above) on press/release, plain gba_buttons sessions
    /// only (!touchInput). Not tracking *which* source holds a given bit --
    /// simultaneously holding the on-screen button and its bound physical
    /// key for the same GBA button, then releasing just one, clears the bit
    /// early. A real if narrow edge case, accepted for now (matches this
    /// pass's scope: get physical input working at all, not exhaustively
    /// arbitrate multiple simultaneous sources per bit).
    func setButton(bit: Int, pressed: Bool) {
        if pressed {
            keymask |= UInt16(bit)
        } else {
            keymask &= ~UInt16(bit)
        }
        client?.sendInput(keymask: keymask)
    }

    /// Called by PlayerKeyInputView for every physical-keyboard press/
    /// release. Returns whether this code matched a binding, so the caller
    /// knows whether to let the press fall through to the normal iOS
    /// responder chain (e.g. Escape-to-dismiss) instead of swallowing it.
    /// Direct port of PlayerActivity.kt's own onKeyDown/onKeyUp +
    /// handleExtKey.
    @discardableResult
    func handlePhysicalKey(code: Int, pressed: Bool) -> Bool {
        if hasButtons, applyExtKeyCode(code, pressed: pressed) {
            return true
        }
        guard let bit = keyCodeToBit[code] else { return false }
        setButton(bit: bit, pressed: pressed)
        return true
    }

    /// hasButtons sessions only -- checks both binding sources
    /// (extKeyCodeToButton: X/Y/ZL/ZR/stick-directions, bound in
    /// KeyBindingsView's own "Erweiterte Tasten" section; and
    /// keyCodeToExtBitFromGba: A/B/L/R/Select/Start/Up/Down/Left/Right,
    /// reusing the Standard-Tasten binding instead of asking for it twice
    /// -- see ExtButtons.kt's GBA_PREFKEY_TO_EXT_BUTTON_BIT). Returns
    /// whether code matched either.
    private func applyExtKeyCode(_ code: Int, pressed: Bool) -> Bool {
        var handled = false
        if let button = extKeyCodeToButton[code] {
            applyExtKey(button, pressed: pressed)
            handled = true
        }
        if let bit = keyCodeToExtBitFromGba[code] {
            setExtButton(bit: UInt32(bit), pressed: pressed)
            handled = true
        }
        return handled
    }

    /// A .button entry ORs/ANDs its bit into extButtons exactly like
    /// ExtHoldButton's on-screen counterpart; a stick-direction entry just
    /// flips the corresponding held-direction flag and re-derives that
    /// stick's combined x/y from all four of its own directions.
    private func applyExtKey(_ button: ExtButton, pressed: Bool) {
        switch button.kind {
        case .button:
            setExtButton(bit: UInt32(button.bit), pressed: pressed)
        case .stickLUp: extKeyStickLUp = pressed; sendLeftStickFromKeys()
        case .stickLDown: extKeyStickLDown = pressed; sendLeftStickFromKeys()
        case .stickLLeft: extKeyStickLLeft = pressed; sendLeftStickFromKeys()
        case .stickLRight: extKeyStickLRight = pressed; sendLeftStickFromKeys()
        case .stickRUp: extKeyStickRUp = pressed; sendRightStickFromKeys()
        case .stickRDown: extKeyStickRDown = pressed; sendRightStickFromKeys()
        case .stickRLeft: extKeyStickRLeft = pressed; sendRightStickFromKeys()
        case .stickRRight: extKeyStickRRight = pressed; sendRightStickFromKeys()
        }
    }

    /// Opposite directions held together cancel out (both false, or both
    /// true, both read as 0) rather than one arbitrarily winning.
    private func sendLeftStickFromKeys() {
        let x: Int16 = extKeyStickLLeft == extKeyStickLRight ? 0 : (extKeyStickLRight ? 32767 : -32767)
        let y: Int16 = extKeyStickLUp == extKeyStickLDown ? 0 : (extKeyStickLUp ? 32767 : -32767)
        setLeftStick(x: x, y: y)
    }

    private func sendRightStickFromKeys() {
        let x: Int16 = extKeyStickRLeft == extKeyStickRRight ? 0 : (extKeyStickRRight ? 32767 : -32767)
        let y: Int16 = extKeyStickRUp == extKeyStickRDown ? 0 : (extKeyStickRUp ? 32767 : -32767)
        setRightStick(x: x, y: y)
    }

    // MARK: - Touch-capable session input (touchInput sessions only)

    /// Routes a touch update through sendTouch (plain "n3ds_touch"
    /// session) or the combined extended-input send (hasButtons, which
    /// needs the current button/stick state resent alongside every touch
    /// change too, not just touch's own) -- same split as
    /// PlayerActivity.kt's own sendTouchState().
    func updateTouch(pressed: Bool, x: UInt16, y: UInt16) {
        if hasButtons {
            extTouchPressed = pressed
            extTouchX = x
            extTouchY = y
            sendCombined()
        } else {
            client?.sendTouch(pressed: pressed, x: x, y: y)
        }
    }

    /// hasButtons sessions only -- the shared GBA-mapped buttons (L/R/
    /// Select/Start/Up/Down/Left/Right/A/B) plus X/Y.
    func setExtButton(bit: UInt32, pressed: Bool) {
        if pressed {
            extButtons |= bit
        } else {
            extButtons &= ~bit
        }
        sendCombined()
    }

    /// hasSticks sessions only. y is already in "positive = pushed up"
    /// convention (VirtualStick's own job, see that view's comment).
    func setLeftStick(x: Int16, y: Int16) {
        extLeftX = x
        extLeftY = y
        sendCombined()
    }

    func setRightStick(x: Int16, y: Int16) {
        extRightX = x
        extRightY = y
        sendCombined()
    }

    /// Updates the extended-input state (buttons/sticks) first, then
    /// triggers the actual frame send via sendTouch -- unison_native_send_
    /// extended_input() only updates the C bridge's persistent state, it
    /// doesn't itself mark anything dirty (see that function's own header
    /// comment); sendTouch()'s dirty flag is what makes maybe_send_touch()
    /// actually build and send a frame, picking up whatever
    /// extButtons/sticks currently hold either way.
    private func sendCombined() {
        client?.sendExtendedInput(buttons: extButtons, leftX: extLeftX, leftY: extLeftY, rightX: extRightX,
                                   rightY: extRightY)
        client?.sendTouch(pressed: extTouchPressed, x: extTouchX, y: extTouchY)
    }

    // MARK: - GbaStreamClient.Listener (fires on the background session thread)

    func onConnected(touchInput: Bool, hasButtons: Bool, hasSticks: Bool, width: Int32, height: Int32,
                      grantedVideoMode: String, streamType: String) {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.status = .connected
            self.touchInput = touchInput
            self.hasButtons = hasButtons
            self.hasSticks = hasSticks
            self.streamWidth = width
            self.streamHeight = height
            self.streamType = streamType
            self.grantedVideoMode = grantedVideoMode
            if let layer = self.displayLayer, self.compressedVideoDecoder == nil {
                self.compressedVideoDecoder = CompressedVideoDecoder(displayLayer: layer,
                                                                      isH265: grantedVideoMode == "h265")
            }
        }
    }

    func onVideoFrame(width: Int32, height: Int32, rgb565: Data) {
        guard let image = Self.image(fromRGB565: rgb565, width: Int(width), height: Int(height)) else {
            return
        }
        DispatchQueue.main.async { [weak self] in
            self?.currentFrame = image
        }
    }

    /// UNISON_VIDEO_FORMAT_H264/_H265 only -- called on the background
    /// session thread (same as onVideoFrame above), not hopped to the main
    /// thread first: AVSampleBufferDisplayLayer.enqueue() is documented
    /// safe to call off the main thread (Apple's own sample code for this
    /// exact low-latency-real-time-source pattern does the same), and this
    /// path is already about as time-sensitive as this app gets -- an
    /// extra runloop turn of latency here would be a real, visible cost
    /// for no benefit.
    func onCompressedVideoFrame(width: Int32, height: Int32, isH265: Bool, data: UnsafeRawBufferPointer) {
        compressedVideoDecoder?.decode(data: data)
    }

    /// Called once by CompressedVideoView.onLayerReady when its backing
    /// AVSampleBufferDisplayLayer is created -- in practice always after
    /// onConnected already ran (the view only exists once grantedVideoMode
    /// is known to be h264/h265, see PlayerView.body), but this handles
    /// either order: builds compressedVideoDecoder here if
    /// grantedVideoMode is already known, otherwise onConnected above
    /// finishes the job once it arrives.
    func setDisplayLayer(_ layer: AVSampleBufferDisplayLayer) {
        displayLayer = layer
        if !grantedVideoMode.isEmpty, compressedVideoDecoder == nil {
            compressedVideoDecoder = CompressedVideoDecoder(displayLayer: layer, isH265: grantedVideoMode == "h265")
        }
    }

    func onAudioFrame(sampleRate: Int32, channels: Int32, pcm: [Int16]) {
        play(pcm: pcm, sampleRate: sampleRate, channels: channels)
    }

    func onDisconnected(reason: String) {
        DispatchQueue.main.async { [weak self] in
            self?.status = .disconnected(reason: reason)
        }
    }

    // MARK: - Video: u16le RGB565 -> UIImage

    /// Manual per-pixel unpack (not the fastest possible path -- a later
    /// pass could move this into a small C helper, same pattern as
    /// unison_native_bridge.c's own inflate/decode work, if a real GBA
    /// frame rate ever shows this mattering) into 8-8-8-8 RGBA, since
    /// CGImage/UIImage have no native RGB565 pixel format. Explicit
    /// byte-pair little-endian decode (not a raw UInt16 reinterpret) --
    /// avoids relying on host-endianness assumptions or Data-alignment
    /// guarantees, matching how the web client's own DataView-based decode
    /// (index.html) stays explicit about it too.
    static func image(fromRGB565 data: Data, width: Int, height: Int) -> UIImage? {
        guard width > 0, height > 0, data.count >= width * height * 2 else { return nil }

        var rgba = [UInt8](repeating: 0, count: width * height * 4)
        data.withUnsafeBytes { (raw: UnsafeRawBufferPointer) in
            let bytes = raw.bindMemory(to: UInt8.self)
            for i in 0..<(width * height) {
                let lo = UInt16(bytes[i * 2])
                let hi = UInt16(bytes[i * 2 + 1])
                let p = lo | (hi << 8)
                let r5 = (p >> 11) & 0x1F
                let g6 = (p >> 5) & 0x3F
                let b5 = p & 0x1F
                let o = i * 4
                rgba[o] = UInt8((r5 << 3) | (r5 >> 2))
                rgba[o + 1] = UInt8((g6 << 2) | (g6 >> 4))
                rgba[o + 2] = UInt8((b5 << 3) | (b5 >> 2))
                rgba[o + 3] = 255
            }
        }

        guard let provider = CGDataProvider(data: Data(rgba) as CFData),
              let cgImage = CGImage(
                  width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
                  bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                  bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
                  provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
        else {
            return nil
        }
        return UIImage(cgImage: cgImage)
    }

    // MARK: - Audio: PCM push playback via AVAudioEngine

    private func ensureAudioEngine(sampleRate: Double, channels: AVAudioChannelCount) {
        guard audioEngine == nil else { return }
        // .pcmFormatFloat32 non-interleaved is AVAudioEngine's own native
        // processing format -- feeding it Int16/interleaved buffers
        // directly is a well-known source of connect()/mixer format
        // mismatches, so play(pcm:) below deinterleaves+normalizes by
        // hand instead of trying to hand Int16 straight to the engine.
        guard let format = AVAudioFormat(standardFormatWithSampleRate: sampleRate, channels: channels) else {
            return
        }
        let engine = AVAudioEngine()
        let player = AVAudioPlayerNode()
        engine.attach(player)
        engine.connect(player, to: engine.mainMixerNode, format: format)
        do {
            try engine.start()
        } catch {
            return
        }
        player.play()
        audioEngine = engine
        playerNode = player
        audioFormat = format
    }

    private func play(pcm: [Int16], sampleRate: Int32, channels: Int32) {
        guard channels > 0, !pcm.isEmpty else { return }
        ensureAudioEngine(sampleRate: Double(sampleRate), channels: AVAudioChannelCount(channels))
        guard let format = audioFormat, let player = playerNode else { return }

        let ch = Int(channels)
        let frameCount = AVAudioFrameCount(pcm.count / ch)
        guard frameCount > 0,
              let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: frameCount),
              let floatData = buffer.floatChannelData
        else {
            return
        }
        buffer.frameLength = frameCount

        for frame in 0..<Int(frameCount) {
            for c in 0..<ch {
                floatData[c][frame] = Float(pcm[frame * ch + c]) / 32768.0
            }
        }
        player.scheduleBuffer(buffer, completionHandler: nil)
    }
}

struct PlayerView: View {
    let host: String
    let port: Int32
    // "" (manual host:port entry, see MenuView's own Connection) means the
    // real stream_type isn't known until the handshake's hello -- passed
    // straight through to PlayerViewModel.connect(), which needs it before
    // that point to request the right per-console video mode (see that
    // method's own comment); NOT the same thing as viewModel.streamType,
    // which is only ever set from the live hello response once actually
    // connected (used for bilinear filtering at render time instead).
    var knownStreamType: String = ""
    // Lets RootView collapse its NavigationSplitView sidebar for the
    // duration of an actual stream (reported directly after real-iPad
    // testing: the sidebar should disappear once the stream is running,
    // not stay docked next to a fullscreen game) without PlayerView needing
    // to know RootView/NavigationSplitView exist at all -- a plain closure
    // rather than @Binding<Bool> so this view stays trivially constructible
    // on its own (#Preview, tests) with nothing to wire up.
    var onActiveChanged: ((Bool) -> Void)? = nil

    @StateObject private var viewModel = PlayerViewModel()
    @Environment(\.dismiss) private var dismiss
    // Separate instance from PlayerViewModel's own private `prefs` (that
    // one is a different type's private property, out of reach from this
    // struct's body) -- both just wrap the same UserDefaults, so two
    // instances are as consistent as one.
    private let prefs = Prefs()

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            if viewModel.grantedVideoMode == "h264" || viewModel.grantedVideoMode == "h265" {
                // AVSampleBufferDisplayLayer does hardware decode+render in
                // one step -- bilinear/nearest-neighbor filtering (the
                // Image branch below applies via .interpolation) has no
                // real equivalent worth wiring up here: videoGravity
                // (CompressedVideoView's own setup) already does the same
                // "scaled to fit, aspect preserved" letterboxing the Image
                // branch's .aspectRatio(contentMode: .fit) does, just via
                // AVFoundation's own mechanism instead of SwiftUI's.
                CompressedVideoView { layer in viewModel.setDisplayLayer(layer) }
            } else if let frame = viewModel.currentFrame {
                // Was unconditionally .none (nearest-neighbor) here,
                // ignoring AntialiasingView's own per-stream-type toggle
                // entirely -- the setting persisted correctly (Prefs.
                // setBilinear) but nothing at render time ever read it
                // back. .medium (not .high): a modest smooth upscale,
                // matching Android's own bilinear (not some sharper/
                // Lanczos-like resampling) filter mode.
                Image(uiImage: frame)
                    .resizable()
                    .interpolation(prefs.bilinear(for: viewModel.streamType) ? .medium : .none)
                    .aspectRatio(contentMode: .fit)
            }

            // Behind the buttons/sticks below (earlier in this ZStack, so
            // they draw underneath and lose the hit-test to whichever
            // control -- if any -- sits on top of a given point).
            if viewModel.touchInput {
                TouchOverlay(streamWidth: viewModel.streamWidth, streamHeight: viewModel.streamHeight) {
                    pressed, x, y in
                    viewModel.updateTouch(pressed: pressed, x: x, y: y)
                }
            }

            VStack {
                statusBar
                Spacer()
            }

            // Each its own ZStack layer, not nested in the VStack above --
            // both need the whole screen's bounds to corner-anchor their
            // buttons/sticks the way PlayerActivity.kt's PlayerScreen()
            // does (real GBA/3DS button placement), not just a bottom
            // row's worth of space. See ButtonOverlay's/
            // ExtendedControlsOverlay's own comments.
            if viewModel.touchInput, viewModel.hasButtons {
                ExtendedControlsOverlay(hasSticks: viewModel.hasSticks, viewModel: viewModel)
            }
            if !viewModel.touchInput {
                ButtonOverlay { bit, pressed in
                    viewModel.setButton(bit: bit, pressed: pressed)
                }
            }

            // Invisible -- just needs to be in the view tree to become
            // first responder and see physical-keyboard presses for the
            // whole session (game-controller input goes through
            // ControllerInputHandler instead, owned by PlayerViewModel
            // directly since it needs no view of its own).
            PlayerKeyInputView { code, pressed in
                viewModel.handlePhysicalKey(code: code, pressed: pressed)
            }
            .frame(width: 1, height: 1)
        }
        .statusBarHidden()
        .onAppear {
            viewModel.connect(host: host, port: port, knownStreamType: knownStreamType)
            // Matches Android's PlayerActivity forcing landscape (that
            // Activity's own manifest entry) -- see OrientationLock.swift
            // for why this needs an AppDelegate hook rather than a direct
            // per-view SwiftUI modifier. Restored to .all on disappear so
            // Menu/Settings go back to following the device's actual
            // orientation, notably relevant on iPad (see project.yml's own
            // comment).
            OrientationLock.mask = .landscape
            UIViewController.attemptRotationToDeviceOrientation()
            onActiveChanged?(true)
        }
        .onDisappear {
            viewModel.disconnect()
            OrientationLock.mask = .all
            UIViewController.attemptRotationToDeviceOrientation()
            onActiveChanged?(false)
        }
    }

    @ViewBuilder
    private var statusBar: some View {
        switch viewModel.status {
        case .connecting:
            Text(LocaleHelper.string("status_connecting", prefs: Prefs()))
                .foregroundStyle(.white)
                .padding(8)
        case .connected:
            EmptyView()
        case .disconnected(let reason):
            let template = LocaleHelper.string("status_disconnected_reason", prefs: Prefs())
            VStack(spacing: 8) {
                // i18n/generate.py's ios_format() writes "%1$@" placeholders
                // (see Resources/*.lproj/Localizable.strings) -- String(format:)
                // is the correct Foundation counterpart, not manual
                // concatenation (which would either duplicate or drop the
                // template's own punctuation around the placeholder).
                Text(String(format: template, reason))
                    .foregroundStyle(.white)
                // .borderless, same reasoning as MenuView's Connect button
                // -- consistent native-iOS text-button look across the app.
                Button(LocaleHelper.string("back", prefs: Prefs())) { dismiss() }
                    .font(.body.bold())
                    .buttonStyle(.borderless)
            }
            .padding(8)
            .background(.black.opacity(0.6))
            .cornerRadius(8)
        }
    }
}

/// Real-time physical-keyboard input for the whole session -- pressesBegan/
/// pressesEnded forwarded for every press, not just the next one (unlike
/// KeyCaptureView, the Settings binding-*capture* UI's one-shot counterpart
/// -- see that file's own comment on the platform's hardware-keyboard-only
/// limitation, which applies here too: a game controller's buttons go
/// through GameController's own API, not UIPress at all, see
/// ControllerInputHandler.swift). `onKey` returns whether the code matched
/// a binding, so an unmatched press (e.g. Escape) still falls through to
/// the normal responder chain instead of being swallowed unconditionally.
private struct PlayerKeyInputView: UIViewRepresentable {
    let onKey: (Int, Bool) -> Bool

    func makeUIView(context: Context) -> KeyInputUIView {
        let view = KeyInputUIView()
        view.onKey = onKey
        return view
    }

    func updateUIView(_ uiView: KeyInputUIView, context: Context) {
        uiView.onKey = onKey
        if uiView.window != nil {
            uiView.becomeFirstResponder()
        }
    }

    final class KeyInputUIView: UIView {
        var onKey: ((Int, Bool) -> Bool)?

        override var canBecomeFirstResponder: Bool { true }

        override func didMoveToWindow() {
            super.didMoveToWindow()
            if window != nil {
                becomeFirstResponder()
            }
        }

        override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
            var unhandled = Set<UIPress>()
            for press in presses {
                guard let key = press.key, onKey?(Int(key.keyCode.rawValue), true) == true else {
                    unhandled.insert(press)
                    continue
                }
            }
            if !unhandled.isEmpty {
                super.pressesBegan(unhandled, with: event)
            }
        }

        override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
            var unhandled = Set<UIPress>()
            for press in presses {
                guard let key = press.key, onKey?(Int(key.keyCode.rawValue), false) == true else {
                    unhandled.insert(press)
                    continue
                }
            }
            if !unhandled.isEmpty {
                super.pressesEnded(unhandled, with: event)
            }
        }
    }
}

/// The 10 GBA_BUTTONS as on-screen hold buttons, corner-anchored to match
/// PlayerActivity.kt's PlayerScreen() layout (L/R top corners, Select/Start
/// top-center, D-pad bottom-leading, A/B bottom-trailing) instead of a
/// single row along the bottom edge -- a plain HStack put every control at
/// the bottom of the screen, cramped together and far from the real GBA's
/// actual button placement (reported directly after real-device testing
/// against Dolphin). HoldButton's own fixed 44x44 circular shape is kept
/// as-is throughout (Android varies shape/size per role -- rounded-rect
/// L/R, pill Select/Start, larger A/B circles) -- this ports the
/// *positions*, not a full shape-for-shape visual redesign. Press/release
/// only (no drag/slide between D-pad directions like Android's own DPad
/// gesture) -- same interaction model as every other on-screen control in
/// this file; a closer 1:1 gesture port is future polish, not required to
/// fix "buttons all crammed at the bottom".
private struct ButtonOverlay: View {
    let onButton: (Int, Bool) -> Void

    private func hold(_ label: String) -> some View {
        // Force-unwrap is safe: GBA_BUTTONS is a fixed, hardcoded 10-entry
        // list (GbaButtons.swift) that always contains exactly these
        // labels -- a typo here would be a build-time-obvious programmer
        // error, not a runtime possibility.
        let entry = GBA_BUTTONS.first { $0.label == label }!
        return HoldButton(label: label) { pressed in onButton(entry.bit, pressed) }
    }

    var body: some View {
        Color.clear
            .overlay(alignment: .topLeading) { hold("L").padding() }
            .overlay(alignment: .topTrailing) { hold("R").padding() }
            .overlay(alignment: .top) {
                HStack(spacing: 12) { hold("Select"); hold("Start") }
                    .padding(.top, 8)
            }
            .overlay(alignment: .bottomLeading) { dPad.padding() }
            .overlay(alignment: .bottomTrailing) { actionButtons.padding() }
    }

    /// Plus-shaped cluster (each direction its own independent press
    /// target) -- matches Android's DPad's corner positions, not its
    /// continuous drag-between-cells gesture.
    private var dPad: some View {
        Color.clear
            .frame(width: 132, height: 132)
            .overlay(alignment: .top) { hold("Up") }
            .overlay(alignment: .bottom) { hold("Down") }
            .overlay(alignment: .leading) { hold("Left") }
            .overlay(alignment: .trailing) { hold("Right") }
    }

    /// B bottom-leading, A top-trailing -- same diagonal as Android's
    /// ActionButtons.
    private var actionButtons: some View {
        Color.clear
            .frame(width: 132, height: 100)
            .overlay(alignment: .bottomLeading) { hold("B") }
            .overlay(alignment: .topTrailing) { hold("A") }
    }
}

/// Hosts an AVSampleBufferDisplayLayer for an h264/h265 session --
/// CompressedVideoDecoder.decode() enqueues straight into the layer this
/// exposes via onLayerReady, called once when the backing UIView is
/// created. A dedicated UIView subclass (not a bare CALayer handed to
/// SwiftUI some other way) since AVSampleBufferDisplayLayer needs to be a
/// real CALayer in a real view hierarchy to actually composite on screen.
private struct CompressedVideoView: UIViewRepresentable {
    let onLayerReady: (AVSampleBufferDisplayLayer) -> Void

    func makeUIView(context: Context) -> DisplayLayerHostView {
        let view = DisplayLayerHostView()
        onLayerReady(view.displayLayer)
        return view
    }

    func updateUIView(_ uiView: DisplayLayerHostView, context: Context) {}

    final class DisplayLayerHostView: UIView {
        let displayLayer = AVSampleBufferDisplayLayer()

        override init(frame: CGRect) {
            super.init(frame: frame)
            // .resizeAspect: scaled to fit, aspect preserved, same
            // letterboxing the raw/tiles path gets from the Image branch's
            // own .aspectRatio(contentMode: .fit) -- see PlayerView.body's
            // comment on why nothing further needs to be wired up here for
            // that.
            displayLayer.videoGravity = .resizeAspect
            layer.addSublayer(displayLayer)
        }

        required init?(coder: NSCoder) {
            fatalError("init(coder:) has not been implemented")
        }

        override func layoutSubviews() {
            super.layoutSubviews()
            displayLayer.frame = bounds
        }
    }
}

/// touchInput && hasButtons sessions (Cemu/Azahar/melonDS): the shared
/// GBA-mapped buttons (GBA_PREFKEY_TO_EXT_BUTTON_BIT -- L/R/Select/Start/
/// Up/Down/Left/Right/A/B) plus X/Y (EXT_BUTTONS), and -- only when the
/// session also has real analog input (hasSticks, Azahar's
/// N3DS_BOTTOM_SCREEN) -- ZL/ZR (EXT_BUTTONS_LIMITED) and both
/// VirtualSticks. Corner-anchored, same visual language as ButtonOverlay's
/// own layout (reported as fixed/working after real-device testing): ZL/ZR
/// top corners, shared L/Select/Start/R top-center, a D-pad-equivalent
/// cluster stacked above the left stick, an A/B/X/Y diamond stacked above
/// the right stick -- replaces the previous single bottom-row layout
/// (reported as still not correct after that same testing pass, unlike the
/// plain-buttons case). Without hasSticks (melonDS's touch_and_buttons: a
/// D-pad but no analog stick), the two clusters just have no stick beneath
/// them and ZL/ZR don't render at all.
private struct ExtendedControlsOverlay: View {
    let hasSticks: Bool
    @ObservedObject var viewModel: PlayerViewModel

    private func hold(_ label: String, bit: UInt32) -> some View {
        ExtHoldButton(label: label, bit: bit, viewModel: viewModel)
    }

    private func shared(_ label: String) -> some View {
        // Force-unwrap is safe: GBA_BUTTONS is a fixed, hardcoded 10-entry
        // list (GbaButtons.swift) that always contains exactly these
        // labels -- a typo here would be a build-time-obvious programmer
        // error, not a runtime possibility (same reasoning as
        // ButtonOverlay.hold(_:)).
        let entry = sharedExtButtons.first { $0.label == label }!
        return hold(entry.label, bit: entry.bit)
    }

    var body: some View {
        Color.clear
            .overlay(alignment: .topLeading) {
                if hasSticks {
                    hold("ZL", bit: UInt32(ExtButtonBit.ZL)).padding()
                }
            }
            .overlay(alignment: .topTrailing) {
                if hasSticks {
                    hold("ZR", bit: UInt32(ExtButtonBit.ZR)).padding()
                }
            }
            .overlay(alignment: .top) {
                HStack(spacing: 12) {
                    shared("L")
                    shared("Select")
                    shared("Start")
                    shared("R")
                }
                .padding(.top, 8)
            }
            .overlay(alignment: .bottomLeading) { leftCluster.padding() }
            .overlay(alignment: .bottomTrailing) { rightCluster.padding() }
    }

    /// D-pad-equivalent (Up/Down/Left/Right), stacked directly above the
    /// left stick when present -- gba_buttons digital directions and the
    /// stream's own analog stick are separate wire fields (protocol.md),
    /// both meaningful to send at once, so this pairs with the stick
    /// rather than replacing it.
    private var leftCluster: some View {
        VStack(spacing: 8) {
            Color.clear
                .frame(width: 132, height: 132)
                .overlay(alignment: .top) { shared("Up") }
                .overlay(alignment: .bottom) { shared("Down") }
                .overlay(alignment: .leading) { shared("Left") }
                .overlay(alignment: .trailing) { shared("Right") }
            if hasSticks {
                Stick { x, y in viewModel.setLeftStick(x: x, y: y) }
            }
        }
    }

    /// A/B/X/Y diamond (X top, Y bottom, B leading, A trailing -- same
    /// shape as PlayerActivity.kt's own ExtActionButtons), stacked directly
    /// above the right stick when present.
    private var rightCluster: some View {
        VStack(spacing: 8) {
            Color.clear
                .frame(width: 132, height: 132)
                .overlay(alignment: .top) { hold("X", bit: UInt32(ExtButtonBit.X)) }
                .overlay(alignment: .bottom) { hold("Y", bit: UInt32(ExtButtonBit.Y)) }
                .overlay(alignment: .leading) { shared("B") }
                .overlay(alignment: .trailing) { shared("A") }
            if hasSticks {
                Stick { x, y in viewModel.setRightStick(x: x, y: y) }
            }
        }
    }

    private var sharedExtButtons: [(label: String, bit: UInt32)] {
        GBA_BUTTONS.compactMap { button in
            guard let bit = GBA_PREFKEY_TO_EXT_BUTTON_BIT[button.prefKey] else { return nil }
            return (button.label, UInt32(bit))
        }
    }
}

private struct ExtHoldButton: View {
    let label: String
    let bit: UInt32
    @ObservedObject var viewModel: PlayerViewModel

    var body: some View {
        HoldButton(label: label) { pressed in
            viewModel.setExtButton(bit: bit, pressed: pressed)
        }
    }
}

private struct HoldButton: View {
    let label: String
    let onPress: (Bool) -> Void

    var body: some View {
        Text(label)
            .font(.headline)
            .frame(width: 44, height: 44)
            .background(Circle().fill(.white.opacity(0.25)))
            .foregroundStyle(.white)
            // minimumDuration: 0 + the `pressing` closure is the standard
            // SwiftUI idiom for "fires on touch-down and touch-up", not an
            // actual long-press -- there's no built-in plain "hold" gesture.
            .onLongPressGesture(minimumDuration: 0, maximumDistance: .infinity) {
                // no-op: only the pressing closure below matters here
            } onPressingChanged: { pressing in
                onPress(pressing)
            }
    }
}

/// The whole video area doubles as the touch surface: press, drag, and
/// release all map 1:1 to unison_touch_state's pressed/x/y (protocol.h).
/// One continuous gesture -- a drag needs to keep reporting positions all
/// the way to release, not just an initial tap. Direct port of
/// PlayerActivity.kt's own TouchOverlay + sendMappedTouch.
private struct TouchOverlay: View {
    let streamWidth: Int32
    let streamHeight: Int32
    let onTouch: (Bool, UInt16, UInt16) -> Void

    var body: some View {
        GeometryReader { geo in
            Color.clear
                .contentShape(Rectangle())
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { value in sendMapped(value.location, in: geo.size) }
                        .onEnded { _ in onTouch(false, 0, 0) }
                )
        }
    }

    /// Maps a position (in this view's own point coordinates) through the
    /// same "centered, scaled to fit, aspect preserved" letterboxing math
    /// as the Image displaying the video underneath (.aspectRatio(contentMode:
    /// .fit)) to the video's native pixel coordinates. A position that
    /// lands in the letterbox bars (mismatched container/content aspect
    /// ratio) clamps to the nearest edge rather than being dropped, so a
    /// drag that wanders there still tracks instead of going silent.
    private func sendMapped(_ position: CGPoint, in containerSize: CGSize) {
        guard containerSize.width > 0, containerSize.height > 0, streamWidth > 0, streamHeight > 0 else {
            return
        }
        let bw = CGFloat(streamWidth)
        let bh = CGFloat(streamHeight)
        let scale = min(containerSize.width / bw, containerSize.height / bh)
        let offsetX = (containerSize.width - bw * scale) / 2
        let offsetY = (containerSize.height - bh * scale) / 2

        let x = min(max((position.x - offsetX) / scale, 0), bw - 1)
        let y = min(max((position.y - offsetY) / scale, 0), bh - 1)
        onTouch(true, UInt16(x), UInt16(y))
    }
}

/// A draggable virtual analog stick -- knob clamped to a circle, reporting
/// (x, y) in unison_extended_input's own -32767...32767 range, y positive
/// = pushed up (screen-space drag is y-down, so this negates it). Direct
/// port of PlayerActivity.kt's own VirtualStick.
private struct Stick: View {
    let onChange: (Int16, Int16) -> Void
    private let radius: CGFloat = 40

    @State private var knobOffset: CGSize = .zero

    var body: some View {
        ZStack {
            Circle().fill(.white.opacity(0.15)).frame(width: radius * 2, height: radius * 2)
            Circle().fill(.white.opacity(0.4)).frame(width: 36, height: 36).offset(knobOffset)
        }
        .contentShape(Circle().size(width: radius * 2, height: radius * 2))
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { value in
                    let dx = value.translation.width
                    let dy = value.translation.height
                    let distance = min((dx * dx + dy * dy).squareRoot(), radius)
                    let angle = atan2(dy, dx)
                    let clampedX = cos(angle) * distance
                    let clampedY = sin(angle) * distance
                    knobOffset = CGSize(width: clampedX, height: clampedY)
                    onChange(Int16((clampedX / radius) * 32767), Int16((-clampedY / radius) * 32767))
                }
                .onEnded { _ in
                    knobOffset = .zero
                    onChange(0, 0)
                }
        )
    }
}
