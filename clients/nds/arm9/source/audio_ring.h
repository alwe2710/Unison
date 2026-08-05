#ifndef UNISON_NDS_AUDIO_RING_H
#define UNISON_NDS_AUDIO_RING_H

#include <stddef.h>
#include <stdint.h>

/* Jitter-buffer ring for GC_GBA_LINK's UNISON_MSG_AUDIO frames -- pulled
 * out of main.c's g_audioRing/audioRingPush/audioStreamRequest globals into
 * its own plain-C, maxmod9.h-free translation unit (main.c's
 * audioStreamRequest() itself stays there, as a thin maxmod-pull-callback
 * wrapper around unison_nds_audio_ring_pull() below) so the actual FIFO
 * logic -- fill order, overflow behavior, underrun padding -- has one place
 * to unit-test on a plain host compiler, see tests/test_audio_ring.c.
 *
 * Not thread-safe by design, matching main.c's own reasoning: the network
 * receive loop (push) and maxmod's pull callback both run on the same
 * thread (mmStreamUpdate() is called from runSession() itself), so despite
 * two "sides" touching this buffer, there's no real concurrency and no
 * locking is needed -- see main.c's own comment on g_audioRing. */

typedef struct {
    int16_t *samples;      /* caller-owned backing storage, capacity elements */
    size_t capacity;
    size_t write_pos;
    size_t read_pos;
    size_t available;      /* samples currently buffered, always <= capacity */
} unison_nds_audio_ring;

void unison_nds_audio_ring_init(unison_nds_audio_ring *ring, int16_t *backing, size_t capacity);

/* Drops all buffered samples without touching the backing storage -- same
 * use as main.c's audioRingReset(), called at the start of each new session
 * so stale samples from before a reconnect can't play at the start of a
 * new one. */
void unison_nds_audio_ring_reset(unison_nds_audio_ring *ring);

/* Pushes sample_count s16le-encoded samples (as they arrive off the wire,
 * see unison_read_s16le) into the ring. On overflow (not enough room for
 * everything), the TAIL of this call's own input is dropped -- not-yet-
 * played samples already in the ring are never overwritten, matching
 * main.c's audioRingPush() own comment. */
void unison_nds_audio_ring_push(unison_nds_audio_ring *ring, const uint8_t *samples_s16le, size_t sample_count);

/* Fills exactly `length` samples into dest, draining the ring FIFO-order.
 * On underrun (fewer than `length` samples buffered), pads the remainder
 * with silence (0) rather than replaying stale samples or leaving dest
 * partially uninitialized -- matches main.c's audioStreamRequest() own
 * comment, which is exactly this function plus the maxmod mm_word/mm_addr
 * call signature wrapped around it. */
void unison_nds_audio_ring_pull(unison_nds_audio_ring *ring, int16_t *dest, size_t length);

#endif /* UNISON_NDS_AUDIO_RING_H */
