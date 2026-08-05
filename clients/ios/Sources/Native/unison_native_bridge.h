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
    // this MVP bridge only ever actually drives a gba_buttons session
    // (touch_input=false), but the fields are still reported for parity/
    // future PlayerView reuse once touch-input streams are wired up too.
    void (*on_connected)(void *user_data, int touch_input, int has_buttons, int extended_input,
                          int32_t width, int32_t height, const char *granted_video_mode);
    // rgb565 is only valid for the duration of this call (owned by the
    // background thread's reusable decode buffer) -- copy it if the
    // callback needs to keep it past returning, same contract as
    // GbaStreamClient.Listener.onVideoFrame's own ByteArray (which Kotlin
    // copies into automatically, unlike this raw pointer).
    void (*on_video_frame)(void *user_data, int32_t width, int32_t height, const uint8_t *rgb565,
                            size_t len);
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
// GbaStreamClient.nativeSendInput/maybe_send_input in jni_bridge.c.
void unison_native_send_input(unison_native_client *client, uint16_t keymask);

// Signals the background thread to stop and blocks until it has (closing
// the socket, freeing `client`) -- same as GbaStreamClient.disconnect()'s
// nativeDisconnect, which pthread_joins for the same reason (documented on
// that Kotlin function: guarantees no further Listener callback fires
// after this returns, so PlayerActivity can safely stop referencing itself
// as the listener right after).
void unison_native_disconnect(unison_native_client *client);
