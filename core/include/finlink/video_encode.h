#ifndef FINLINK_VIDEO_ENCODE_H
#define FINLINK_VIDEO_ENCODE_H

#include <stddef.h>
#include <stdint.h>

/* Server-side counterpart to finlink_decode_video_frame() (finlink/protocol.h)
 * -- encodes an outgoing video frame using the same TILES delta-encoding
 * every client already decodes, plus frame dedup (identical to the previous
 * frame -> caller sends nothing), instead of always sending a full,
 * non-tiled frame. No I/O, no allocation: the caller owns every buffer,
 * same convention as finlink_deflate_raw() (finlink/deflate.h), which this
 * wraps. */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* out_format, out_buf, and *out_size hold a frame to send. */
    FINLINK_ENCODE_OK = 0,
    /* current_rgb565 is pixel-identical to previous_rgb565 -- per
     * docs/protocol.md's "Frame semantics (video dedup)", the caller
     * should send nothing at all this tick, not an empty/zero-tile frame. */
    FINLINK_ENCODE_UNCHANGED = 1,
    /* scratch_capacity or out_capacity was too small. */
    FINLINK_ENCODE_ERR = -1
} finlink_encode_status;

/* Upper bound on finlink_encode_video_frame()'s compressed out_buf size for
 * a given width/height -- use this to size out_buf. Reuses
 * finlink_video_max_inflated_size() (finlink/protocol.h) as the pre-deflate
 * size bound, since a TILES-encoded frame's unpacked size is bounded by
 * exactly the same worst case (every tile changed) that function already
 * accounts for. */
size_t finlink_video_encode_max_size(uint32_t width, uint32_t height);

/* Encodes current_rgb565 (width*height u16le RGB565 pixels, row-major) as
 * the format byte + compressed payload that follow the
 * [type][width][height] header in docs/protocol.md's video frame wire
 * format.
 *
 * previous_rgb565 is the last frame this function returned
 * FINLINK_ENCODE_OK for (or NULL, for a session's first frame, which
 * always encodes a full, non-tiled keyframe -- see docs/protocol.md).
 * When non-NULL, it must be width*height*2 bytes of RGB565 at the SAME
 * width/height as current_rgb565 -- a resolution change mid-session must
 * be signaled to this function as previous_rgb565 = NULL (forcing a fresh
 * keyframe), not by passing a differently-sized buffer.
 *
 * This function does not retain state: on FINLINK_ENCODE_OK, the caller is
 * responsible for copying current_rgb565 into whatever buffer it passes as
 * previous_rgb565 on the next call (memcpy or swap two buffers -- caller's
 * choice), and for keeping that copy unchanged across FINLINK_ENCODE_UNCHANGED
 * results (nothing to update, since nothing was sent).
 *
 * scratch_buf/scratch_capacity is working space for the pre-deflate TILES
 * block (scratch_capacity must be >= finlink_video_max_inflated_size(width,
 * height), finlink/protocol.h) -- never written to out_buf, freed to reuse
 * by the caller as soon as this call returns.
 *
 * out_buf/out_capacity (sized with finlink_video_encode_max_size()) receives
 * the raw-deflate-compressed bytes; *out_size and *out_format are only
 * written on FINLINK_ENCODE_OK. */
finlink_encode_status finlink_encode_video_frame(const uint8_t *current_rgb565,
                                                   const uint8_t *previous_rgb565, uint32_t width,
                                                   uint32_t height, uint8_t *scratch_buf,
                                                   size_t scratch_capacity, uint8_t *out_buf,
                                                   size_t out_capacity, size_t *out_size,
                                                   uint8_t *out_format);

#ifdef __cplusplus
}
#endif

#endif /* FINLINK_VIDEO_ENCODE_H */
