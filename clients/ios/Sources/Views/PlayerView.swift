import AVFoundation
import SwiftUI
import UIKit

/// Streaming screen -- direct-enough analog of PlayerActivity.kt. Renders
/// decoded RGB565 frames, plays PCM audio via AVAudioEngine, and sends
/// input: plain gba_buttons for GC_GBA_LINK, or touch (+ buttons + analog
/// sticks, depending on what the session actually negotiated) for
/// Cemu/Azahar/melonDS. Still no mic or h264/h265 (see clients/ios/
/// README.md's "Phasing").
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

    private var client: GbaStreamClient?
    private var keymask: UInt16 = 0
    private let prefs = Prefs()

    // Extended-input (touchInput && hasButtons) state: touch, buttons, and
    // both analog sticks all merge into ONE unison_extended_input/
    // unison_touch_and_buttons frame per change (see GbaStreamClient.
    // sendExtendedInput's own comment) -- every source that changes any
    // one of these calls sendCombined() below, which resends all of them
    // together, not just its own piece. Direct port of PlayerActivity.kt's
    // own extTouchPressed/extButtons/... state, minus the physical-key
    // contribution (KeyBindingsView isn't wired into PlayerView yet -- a
    // later addition, not this pass).
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

    func connect(host: String, port: Int32) {
        let client = GbaStreamClient(listener: self)
        self.client = client
        client.connect(host: host, port: port, videoMode: prefs.videoMode)
    }

    func disconnect() {
        client?.disconnect()
        client = nil
        audioEngine?.stop()
        audioEngine = nil
        playerNode = nil
        audioFormat = nil
    }

    /// Called from PlayerView's button views on press/release, plain
    /// gba_buttons sessions only (!touchInput).
    func setButton(bit: Int, pressed: Bool) {
        if pressed {
            keymask |= UInt16(bit)
        } else {
            keymask &= ~UInt16(bit)
        }
        client?.sendInput(keymask: keymask)
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
                      grantedVideoMode: String) {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.status = .connected
            self.touchInput = touchInput
            self.hasButtons = hasButtons
            self.hasSticks = hasSticks
            self.streamWidth = width
            self.streamHeight = height
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

    @StateObject private var viewModel = PlayerViewModel()
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            if let frame = viewModel.currentFrame {
                Image(uiImage: frame)
                    .resizable()
                    .interpolation(.none) // native-resolution pixel art, see Prefs.defaultBilinear's own comment
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
                if viewModel.touchInput, viewModel.hasButtons {
                    ExtendedControlsOverlay(hasSticks: viewModel.hasSticks, viewModel: viewModel)
                }
            }

            // Its own ZStack layer, not nested in the VStack above -- it
            // needs the whole screen's bounds to position L/R/Select/Start/
            // D-pad/A/B the way PlayerActivity.kt's PlayerScreen() does
            // (real GBA button placement, corners + edges), not just a
            // bottom row's worth of space. See ButtonOverlay's own comment.
            if !viewModel.touchInput {
                ButtonOverlay { bit, pressed in
                    viewModel.setButton(bit: bit, pressed: pressed)
                }
            }
        }
        .statusBarHidden()
        .onAppear {
            viewModel.connect(host: host, port: port)
            // Matches Android's PlayerActivity forcing landscape (that
            // Activity's own manifest entry) -- see OrientationLock.swift
            // for why this needs an AppDelegate hook rather than a direct
            // per-view SwiftUI modifier. Restored to .all on disappear so
            // Menu/Settings go back to following the device's actual
            // orientation, notably relevant on iPad (see project.yml's own
            // comment).
            OrientationLock.mask = .landscape
            UIViewController.attemptRotationToDeviceOrientation()
        }
        .onDisappear {
            viewModel.disconnect()
            OrientationLock.mask = .all
            UIViewController.attemptRotationToDeviceOrientation()
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

/// touchInput && hasButtons sessions (Cemu/Azahar/melonDS): the shared
/// GBA-mapped buttons (GBA_PREFKEY_TO_EXT_BUTTON_BIT -- L/R/Select/Start/
/// Up/Down/Left/Right/A/B) plus X/Y (EXT_BUTTONS), and -- only when the
/// session also has real analog input (hasSticks, Azahar's
/// N3DS_BOTTOM_SCREEN) -- ZL/ZR (EXT_BUTTONS_LIMITED) and both
/// VirtualSticks. A single wrapped row rather than PlayerActivity.kt's
/// own diamond/D-pad/corner-anchored layout -- functionally complete
/// (every button/stick reachable), simplified layout given this is
/// already a large feature addition; a closer visual port is a later
/// polish pass, not blocking touch input from working at all.
private struct ExtendedControlsOverlay: View {
    let hasSticks: Bool
    @ObservedObject var viewModel: PlayerViewModel

    var body: some View {
        HStack(alignment: .bottom) {
            if hasSticks {
                Stick { x, y in viewModel.setLeftStick(x: x, y: y) }
            }
            Spacer()
            VStack(spacing: 8) {
                if hasSticks {
                    HStack {
                        ExtHoldButton(label: "ZL", bit: UInt32(ExtButtonBit.ZL), viewModel: viewModel)
                        ExtHoldButton(label: "ZR", bit: UInt32(ExtButtonBit.ZR), viewModel: viewModel)
                    }
                }
                LazyVGrid(columns: Array(repeating: GridItem(.fixed(44)), count: 6), spacing: 8) {
                    ForEach(sharedExtButtons, id: \.label) { entry in
                        ExtHoldButton(label: entry.label, bit: entry.bit, viewModel: viewModel)
                    }
                    ExtHoldButton(label: "X", bit: UInt32(ExtButtonBit.X), viewModel: viewModel)
                    ExtHoldButton(label: "Y", bit: UInt32(ExtButtonBit.Y), viewModel: viewModel)
                }
            }
            if hasSticks {
                Spacer()
                Stick { x, y in viewModel.setRightStick(x: x, y: y) }
            }
        }
        .padding()
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
