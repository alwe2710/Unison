#pragma once
// Plain-C bridge between Swift and unison_core: owns the raw POSIX socket,
// runs the connect/handshake/receive loop on a background pthread, and
// calls back into Swift (via C function pointers, not JNI env/jobject --
// Swift has no JNI-equivalent glue to write, just a bridging header, see
// clients/ios/README.md) for video/audio/connection events. All protocol/
// codec logic (WS handshake+framing, message parsing, deflate) lives in
// core/ -- this file is deliberately "dumb": I/O and callback plumbing
// only, mirroring clients/android/.../jni_bridge.c's own split (that file
// is this one's model; see this file's own top comment in the .c for
// exactly what's intentionally NOT ported yet).
#include <stddef.h>
#include <stdint.h>

typedef struct unison_native_client unison_native_client;

typedef struct {
    void *user_data;
    // touch_input/has_buttons/extended_input mirror GbaStreamClient.
    // Listener.onConnected's own params (see that interface's comment) --
    // touch_input selects between unison_native_send_input() (gba_buttons)
    // and unison_native_send_touch()/unison_native_send_extended_input()
    // (any touch-capable session); has_buttons/extended_input further pick
    // which of the three touch-frame shapes unison_native_send_touch()
    // itself builds, same three-way selection as jni_bridge.c's own
    // maybe_send_touch().
    // stream_type: hello.stream_type verbatim ("GC_GBA_LINK",
    // "N3DS_BOTTOM_SCREEN", ...) -- needed alongside touch_input/
    // has_buttons/extended_input because two different stream types
    // (WIIU_GAMEPAD, N3DS_BOTTOM_SCREEN) share the exact same
    // touch_input/has_buttons/extended_input=1/1/1 shape; only the raw
    // string actually distinguishes Cemu from Azahar. Valid only for the
    // duration of this call, same "copy it if you need to keep it"
    // contract as on_video_frame's rgb565.
    void (*on_connected)(void *user_data, int touch_input, int has_buttons, int extended_input,
                          int32_t width, int32_t height, const char *granted_video_mode,
                          const char *stream_type);
    // rgb565 is only valid for the duration of this call (owned by the
    // background thread's reusable decode buffer) -- copy it if the
    // callback needs to keep it past returning, same contract as
    // GbaStreamClient.Listener.onVideoFrame's own ByteArray (which Kotlin
    // copies into automatically, unlike this raw pointer).
    void (*on_video_frame)(void *user_data, int32_t width, int32_t height, const uint8_t *rgb565,
                            size_t len);
    // UNISON_VIDEO_FORMAT_H264/_H265 only (mutually exclusive with
    // on_video_frame above, see handle_video_message()'s own format
    // branch): data is a raw Annex-B NAL stream straight from the server's
    // encoder, NOT raw-deflate -- fed directly to a Swift-owned
    // VTDecompressionSession/AVSampleBufferDisplayLayer instead of through
    // unison_inflate_raw()/unison_decode_video_frame(), same reasoning as
    // jni_bridge.c's handle_h264_h265_video_message() feeding MediaCodec
    // directly. width/height are the encoder's *coded* (padded,
    // macroblock/CTU-aligned) dimensions, not necessarily the stream's
    // display size -- see SoftwareVideoEncoder::CodedWidth()'s own comment
    // on the sibling host repos, this is that same value. is_h265 is 0 for
    // UNISON_VIDEO_FORMAT_H264, nonzero for UNISON_VIDEO_FORMAT_H265 --
    // picks CMVideoFormatDescriptionCreateFromH264ParameterSets vs.
    // ...FromHEVCParameterSets on the Swift side. data is only valid for
    // the duration of this call, same "copy it if you need to keep it"
    // contract as on_video_frame's rgb565 above.
    void (*on_compressed_video_frame)(void *user_data, int32_t width, int32_t height, int is_h265,
                                       const uint8_t *data, size_t len);
    // pcm is signed 16-bit little-endian host-order samples, interleaved
    // if channels > 1 -- same shape as GbaStreamClient.Listener.onAudioFrame's
    // ShortArray. Same "valid only for this call" contract as rgb565 above.
    void (*on_audio_frame)(void *user_data, int32_t sample_rate, int32_t channels,
                            const int16_t *pcm, size_t sample_count);
    void (*on_disconnected)(void *user_data, const char *reason);
} unison_native_callbacks;

// Spawns a background thread; connect result arrives via
// on_connected/on_disconnected, same as GbaStreamClient.connect(). Returns
// NULL only on a local allocation/thread-spawn failure, not a connection
// failure -- a bad host/port/rejected handshake reports through
// on_disconnected instead, exactly like the Kotlin API.
unison_native_client *unison_native_connect(const char *host, int port, const char *video_mode,
                                             unison_native_callbacks callbacks);

// Sets the current gba_buttons keymask (protocol.h's unison_key bits,
// OR'd together) -- resent (only if changed) once per session-loop
// iteration, same "latest wins" contract as
// GbaStreamClient.nativeSendInput/maybe_send_input in jni_bridge.c. Only
// meaningful for a session where on_connected reported touch_input=0.
void unison_native_send_input(unison_native_client *client, uint16_t keymask);

// Sets the current touch state for a touch-capable session (on_connected
// reported touch_input!=0) -- x/y are ignored (sent as 0) whenever pressed
// is 0, same convention as unison_touch_state itself (protocol.h). Which
// of the three wire shapes this actually sends (n3ds_touch/
// touch_and_buttons/n3ds_touch_and_buttons) is picked automatically from
// has_buttons/extended_input as reported by on_connected, same as
// jni_bridge.c's own maybe_send_touch(). buttons/stick state (set
// separately via unison_native_send_extended_input(), only meaningful
// when has_buttons!=0) rides along in the touch_and_buttons/
// n3ds_touch_and_buttons frame shapes.
void unison_native_send_touch(unison_native_client *client, int pressed, uint16_t x, uint16_t y);

// Sets the current buttons/analog-stick state -- only meaningful for a
// has_buttons (touch_and_buttons/n3ds_touch_and_buttons) session; left/
// right stick values are only ever sent (non-zero in the wire frame) when
// extended_input was also reported true, matching
// unison_extended_input's own right-stick-always-(0,0)-otherwise
// convention (protocol.h). Buttons is a unison_button_bit bitmask.
// Doesn't itself trigger a send -- rides along with whatever
// unison_native_send_touch() last set (buttons/sticks aren't gated on
// touch's own pressed state, e.g. holding a button with no finger on the
// touch area at all is a real, valid, independent input -- same as
// jni_bridge.c's own nativeSendExtendedInput/maybe_send_touch split).
void unison_native_send_extended_input(unison_native_client *client, uint32_t buttons, int16_t left_x,
                                        int16_t left_y, int16_t right_x, int16_t right_y);

// Signals the background thread to stop and blocks until it has (closing
// the socket, freeing `client`) -- same as GbaStreamClient.disconnect()'s
// nativeDisconnect, which pthread_joins for the same reason (documented on
// that Kotlin function: guarantees no further Listener callback fires
// after this returns, so PlayerActivity can safely stop referencing itself
// as the listener right after).
void unison_native_disconnect(unison_native_client *client);
