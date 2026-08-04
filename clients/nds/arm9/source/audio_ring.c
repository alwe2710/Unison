#include "audio_ring.h"

#include "finlink/endian.h"

void finlink_nds_audio_ring_init(finlink_nds_audio_ring *ring, int16_t *backing, size_t capacity) {
    ring->samples = backing;
    ring->capacity = capacity;
    finlink_nds_audio_ring_reset(ring);
}

void finlink_nds_audio_ring_reset(finlink_nds_audio_ring *ring) {
    ring->write_pos = 0;
    ring->read_pos = 0;
    ring->available = 0;
}

void finlink_nds_audio_ring_push(finlink_nds_audio_ring *ring, const uint8_t *samples_s16le, size_t sample_count) {
    for (size_t i = 0; i < sample_count; i++) {
        if (ring->available >= ring->capacity) {
            break; /* overflow: drop the tail rather than overwrite not-yet-played samples */
        }
        ring->samples[ring->write_pos] = finlink_read_s16le(samples_s16le + i * 2);
        ring->write_pos = (ring->write_pos + 1) % ring->capacity;
        ring->available++;
    }
}

void finlink_nds_audio_ring_pull(finlink_nds_audio_ring *ring, int16_t *dest, size_t length) {
    size_t take = (length < ring->available) ? length : ring->available;
    for (size_t i = 0; i < take; i++) {
        dest[i] = ring->samples[ring->read_pos];
        ring->read_pos = (ring->read_pos + 1) % ring->capacity;
    }
    for (size_t i = take; i < length; i++) {
        dest[i] = 0;
    }
    ring->available -= take;
}
