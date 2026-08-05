import AVFoundation
import SwiftUI

/// MVP streaming screen -- direct-enough analog of PlayerActivity.kt for
/// this phase's scope (GC_GBA_LINK, gba_buttons only: no touch overlay, no
/// mic, no h264/h265, see clients/ios/README.md's "Phasing"). Renders
/// decoded RGB565 frames, plays PCM audio via AVAudioEngine, and sends the
/// 10 GBA_BUTTONS as on-screen hold buttons.
final class PlayerViewModel: NSObject, ObservableObject, GbaStreamClient.Listener {
    enum Status: Equatable {
        case connecting
        case connected
        case disconnected(reason: String)
    }

    @Published private(set) var status: Status = .connecting
    @Published private(set) var currentFrame: UIImage?

    private var client: GbaStreamClient?
    private var keymask: UInt16 = 0
    private let prefs = Prefs()

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

    /// Called from PlayerView's button views on press/release.
    func setButton(bit: Int, pressed: Bool) {
        if pressed {
            keymask |= UInt16(bit)
        } else {
            keymask &= ~UInt16(bit)
        }
        client?.sendInput(keymask: keymask)
    }

    // MARK: - GbaStreamClient.Listener (fires on the background session thread)

    func onConnected(touchInput: Bool, hasButtons: Bool, hasSticks: Bool, width: Int32, height: Int32,
                      grantedVideoMode: String) {
        DispatchQueue.main.async { [weak self] in
            self?.status = .connected
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

            VStack {
                statusBar
                Spacer()
                ButtonOverlay { bit, pressed in
                    viewModel.setButton(bit: bit, pressed: pressed)
                }
            }
        }
        .statusBarHidden()
        .onAppear { viewModel.connect(host: host, port: port) }
        .onDisappear { viewModel.disconnect() }
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
                Button(LocaleHelper.string("back", prefs: Prefs())) { dismiss() }
                    .buttonStyle(.borderedProminent)
            }
            .padding(8)
            .background(.black.opacity(0.6))
            .cornerRadius(8)
        }
    }
}

/// The 10 GBA_BUTTONS as on-screen hold buttons -- press/release only
/// (no drag/slide between buttons yet, unlike PlayerActivity's
/// TouchOverlay), same MVP scope as the rest of this phase.
private struct ButtonOverlay: View {
    let onButton: (Int, Bool) -> Void

    var body: some View {
        HStack {
            ForEach(GBA_BUTTONS) { button in
                HoldButton(label: button.label) { pressed in
                    onButton(button.bit, pressed)
                }
            }
        }
        .padding()
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
