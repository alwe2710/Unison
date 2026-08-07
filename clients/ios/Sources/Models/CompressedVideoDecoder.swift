import AVFoundation
import CoreMedia
import VideoToolbox

/// Decodes and displays a raw Annex-B H.264/H.265 elementary stream (one
/// UNISON_MSG_VIDEO message's compressed_data per decode(), see
/// unison_native_bridge.h's on_compressed_video_frame) via
/// AVSampleBufferDisplayLayer -- hardware decode and render happen together
/// in one step, no manual VTDecompressionSession needed. This is the
/// standard modern replacement for that lower-level API when the
/// destination is just "draw this on screen" rather than "give me raw
/// pixel buffers to process further", functionally the same role
/// jni_bridge.c's MediaCodec-in-Surface-mode plays on Android (decode
/// straight into something displayable, no CPU-side pixel copy).
///
/// A CMVideoFormatDescription is (re)built from the SPS/PPS (H.264) or
/// VPS/SPS/PPS (H.265) NALs embedded in every keyframe -- every host
/// repo's SoftwareVideoEncoder sets b_repeat_headers=1 specifically so
/// those repeat before each forced keyframe, not just once at session
/// start (see docs/protocol.md's "Keyframe discipline") -- and reused for
/// every delta frame in between, since AVSampleBufferDisplayLayer needs
/// *a* valid description on every sample, not a freshly-rebuilt one each
/// time.
///
/// Not thread-safe by itself -- like GbaStreamClient's other callbacks,
/// decode(data:isH265:) is only ever called from the background session
/// thread (see unison_native_bridge.c's run_session_loop), never
/// concurrently with itself.
final class CompressedVideoDecoder {
    private let displayLayer: AVSampleBufferDisplayLayer
    private var formatDescription: CMVideoFormatDescription?
    // Sticky per decoder instance: PlayerViewModel constructs a fresh one
    // per session (see PlayerView.swift), so this never needs to change
    // mid-lifetime -- a session negotiates exactly one video_mode for its
    // whole duration.
    private let isH265: Bool

    init(displayLayer: AVSampleBufferDisplayLayer, isH265: Bool) {
        self.displayLayer = displayLayer
        self.isH265 = isH265
    }

    /// data is one message's whole compressed_data blob, Annex-B
    /// start-code-delimited (00 00 01 or 00 00 00 01 before each NAL) --
    /// may contain multiple NALs: parameter sets (SPS/PPS, or VPS/SPS/PPS
    /// for H.265) plus a slice for a keyframe, or just a slice for a delta
    /// frame. Only valid for the duration of this call (mirrors the C
    /// callback's own "copy it if you need it past returning" contract) --
    /// this function never retains `data` itself, only copies extracted
    /// out of it.
    func decode(data: UnsafeRawBufferPointer) {
        let nals = Self.splitAnnexB(data)
        guard !nals.isEmpty else { return }

        var parameterSets: [Data] = []
        var sliceNALs: [Data] = []
        for nal in nals {
            guard let first = nal.first else { continue }
            // H.264 (Annex-B NAL header, 1 byte): type is the low 5 bits.
            // H.265 (2-byte NAL header): type is bits 1-6 of the first
            // byte. 7/8 (H.264 SPS/PPS) and 32/33/34 (H.265 VPS/SPS/PPS)
            // are the standard ITU-T H.264/H.265 type values.
            let nalType = isH265 ? (Int(first) >> 1) & 0x3F : Int(first) & 0x1F
            let isParameterSet = isH265 ? (nalType == 32 || nalType == 33 || nalType == 34)
                                         : (nalType == 7 || nalType == 8)
            if isParameterSet {
                parameterSets.append(nal)
            } else {
                sliceNALs.append(nal)
            }
        }

        // A keyframe's parameter sets replace whatever format description
        // this decoder was already using -- a fresh SPS/PPS is exactly
        // what "the encoder forced a keyframe, possibly with a rebuilt
        // encoder context" (SoftwareVideoEncoder's own resolution-change
        // rebuild, see its Width()/Height() comment on the host repos)
        // means, and AVSampleBufferDisplayLayer has no other way to learn
        // a mid-session format change.
        if !parameterSets.isEmpty, let rebuilt = Self.makeFormatDescription(parameterSets: parameterSets, isH265: isH265) {
            formatDescription = rebuilt
        }
        guard let formatDescription, !sliceNALs.isEmpty,
              let sampleBuffer = Self.makeSampleBuffer(nals: sliceNALs, formatDescription: formatDescription)
        else {
            return
        }

        if displayLayer.status == .failed {
            // A prior malformed/out-of-order sample can leave the layer in
            // a permanently failed state until flushed -- self-heals the
            // same way a forced keyframe self-heals a dropped frame
            // elsewhere in this pipeline (docs/protocol.md's "Keyframe
            // discipline"): the next keyframe's fresh format description
            // (handled above) plus this flush gives the layer a clean
            // slate to resume from.
            displayLayer.flush()
        }
        displayLayer.enqueue(sampleBuffer)
    }

    // MARK: - Annex-B parsing

    /// Splits a start-code-delimited Annex-B stream into individual NAL
    /// units, start codes stripped -- handles both 3-byte (00 00 01) and
    /// 4-byte (00 00 00 01) start codes, matching what x264/x265's
    /// b_annexb=1 output actually emits (SoftwareVideoEncoder.cpp, ported
    /// identically across all four host repos). Finds every 3-byte "00 00
    /// 01" occurrence (a 4-byte start code always contains one, one byte
    /// in), then trims trailing zero byte(s) off each extracted NAL --
    /// those belong to the *next* NAL's 4-byte start code, not this NAL's
    /// own content, since real NAL payloads never legally end that way
    /// (the same convention every standard Annex-B parser uses).
    private static func splitAnnexB(_ data: UnsafeRawBufferPointer) -> [Data] {
        guard let base = data.baseAddress else { return [] }
        let bytes = data.bindMemory(to: UInt8.self)
        let count = bytes.count

        var starts: [Int] = [] // index right after each "00 00 01" match
        var i = 0
        while i + 2 < count {
            if bytes[i] == 0, bytes[i + 1] == 0, bytes[i + 2] == 1 {
                starts.append(i + 3)
                i += 3
            } else {
                i += 1
            }
        }
        guard !starts.isEmpty else { return [] }

        var result: [Data] = []
        result.reserveCapacity(starts.count)
        for (idx, start) in starts.enumerated() {
            // The next start code's own "00 00 01" match position is 3
            // bytes *into* that start code -- back up to right after this
            // NAL's actual last content byte.
            var end = idx + 1 < starts.count ? starts[idx + 1] - 3 : count
            while end > start, bytes[end - 1] == 0 { end -= 1 }
            guard end > start else { continue }
            result.append(Data(bytes: base.advanced(by: start), count: end - start))
        }
        return result
    }

    // MARK: - Format description (parameter sets)

    private static func makeFormatDescription(parameterSets: [Data], isH265: Bool) -> CMVideoFormatDescription? {
        // withUnsafeBufferPointer nesting (one per parameter set) via a
        // recursive helper, since CMVideoFormatDescriptionCreateFrom*
        // needs one contiguous array of pointers alive for the single
        // duration of the call -- Swift has no built-in "give me N nested
        // withUnsafeBytes scopes from an array" primitive.
        func withPointers(_ sets: [Data], _ collected: [UnsafePointer<UInt8>], _ sizes: [Int],
                          _ body: ([UnsafePointer<UInt8>], [Int]) -> CMVideoFormatDescription?) -> CMVideoFormatDescription? {
            guard let first = sets.first else {
                return body(collected, sizes)
            }
            return first.withUnsafeBytes { (raw: UnsafeRawBufferPointer) -> CMVideoFormatDescription? in
                guard let ptr = raw.bindMemory(to: UInt8.self).baseAddress else { return nil }
                return withPointers(Array(sets.dropFirst()), collected + [ptr], sizes + [first.count], body)
            }
        }

        return withPointers(parameterSets, [], []) { pointers, sizes in
            var description: CMVideoFormatDescription?
            let status: OSStatus
            if isH265 {
                status = pointers.withUnsafeBufferPointer { pointerBuf in
                    sizes.withUnsafeBufferPointer { sizeBuf in
                        CMVideoFormatDescriptionCreateFromHEVCParameterSets(
                            allocator: kCFAllocatorDefault, parameterSetCount: pointers.count,
                            parameterSetPointers: pointerBuf.baseAddress!, parameterSetSizes: sizeBuf.baseAddress!,
                            nalUnitHeaderLength: 4, extensions: nil, formatDescriptionOut: &description)
                    }
                }
            } else {
                status = pointers.withUnsafeBufferPointer { pointerBuf in
                    sizes.withUnsafeBufferPointer { sizeBuf in
                        CMVideoFormatDescriptionCreateFromH264ParameterSets(
                            allocator: kCFAllocatorDefault, parameterSetCount: pointers.count,
                            parameterSetPointers: pointerBuf.baseAddress!, parameterSetSizes: sizeBuf.baseAddress!,
                            nalUnitHeaderLength: 4, formatDescriptionOut: &description)
                    }
                }
            }
            return status == noErr ? description : nil
        }
    }

    // MARK: - Sample buffer (slice NALs)

    /// Builds one CMSampleBuffer covering every slice NAL in this message
    /// (AVCC length-prefixed, 4-byte big-endian lengths -- matches
    /// nalUnitHeaderLength: 4 above) -- a keyframe message can carry
    /// multiple slice NALs in principle, though every encoder here only
    /// ever emits one per frame in practice; this stays correct either
    /// way.
    private static func makeSampleBuffer(nals: [Data], formatDescription: CMVideoFormatDescription) -> CMSampleBuffer? {
        var avcc = Data()
        avcc.reserveCapacity(nals.reduce(0) { $0 + 4 + $1.count })
        for nal in nals {
            var length = UInt32(nal.count).bigEndian
            withUnsafeBytes(of: &length) { avcc.append(contentsOf: $0) }
            avcc.append(nal)
        }

        var blockBuffer: CMBlockBuffer?
        var status = CMBlockBufferCreateWithMemoryBlock(
            allocator: kCFAllocatorDefault, memoryBlock: nil, blockLength: avcc.count,
            blockAllocator: kCFAllocatorDefault, customBlockSource: nil, offsetToData: 0,
            dataLength: avcc.count, flags: 0, blockBufferOut: &blockBuffer)
        guard status == kCMBlockBufferNoErr, let blockBuffer else { return nil }

        status = avcc.withUnsafeBytes { raw in
            CMBlockBufferReplaceDataBytes(with: raw.baseAddress!, blockBuffer: blockBuffer, offsetIntoDestination: 0,
                                           dataLength: avcc.count)
        }
        guard status == kCMBlockBufferNoErr else { return nil }

        var sampleBuffer: CMSampleBuffer?
        var sampleSize = avcc.count
        // Real-time low-latency: no meaningful PTS/DTS/duration of our own
        // (the server doesn't send timestamps, see docs/protocol.md) --
        // kCMTimingInfoInvalid plus the DisplayImmediately attachment set
        // below is the standard combination for "decode and show this the
        // instant it's ready", not paced playback against a clock.
        status = CMSampleBufferCreateReady(
            allocator: kCFAllocatorDefault, dataBuffer: blockBuffer, formatDescription: formatDescription,
            sampleCount: 1, sampleTimingEntryCount: 1, sampleTimingArray: [CMSampleTimingInfo.invalid],
            sampleSizeEntryCount: 1, sampleSizeArray: &sampleSize, sampleBufferOut: &sampleBuffer)
        guard status == noErr, let sampleBuffer else { return nil }

        // Standard idiom for "decode and display the instant it's ready"
        // (no PTS-based pacing) with AVSampleBufferDisplayLayer -- the
        // attachments array always has exactly one dictionary per sample
        // once createIfNecessary makes it non-nil (one entry per sample in
        // the buffer, and sampleCount is always 1 here).
        if let attachmentsArray = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, createIfNecessary: true) {
            let dictionary = unsafeBitCast(CFArrayGetValueAtIndex(attachmentsArray, 0), to: CFMutableDictionary.self)
            CFDictionarySetValue(dictionary,
                                  Unmanaged.passUnretained(kCMSampleAttachmentKey_DisplayImmediately).toOpaque(),
                                  Unmanaged.passUnretained(kCFBooleanTrue).toOpaque())
        }

        return sampleBuffer
    }
}
