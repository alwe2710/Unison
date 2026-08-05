#include "audio_ring.h"

#include "unison/endian.h"

void unison_nds_audio_ring_init(unison_nds_audio_ring *ring, int16_t *backing, size_t capacity) {
    ring->samples = backing;
    ring->capacity = capacity;
    unison_nds_audio_ring_reset(ring);
}

void unison_nds_audio_ring_reset(unison_nds_audio_ring *ring) {
    ring->write_pos = 0;
    ring->read_pos = 0;
    ring->available = 0;
}

void unison_nds_audio_ring_push(unison_nds_audio_ring *ring, const uint8_t *samples_s16le, size_t sample_count) {
    for (size_t i = 0; i < sample_count; i++) {
        if (ring->available >= ring->capacity) {
            break; /* overflow: drop the tail rather than overwrite not-yet-played samples */
        }
        ring->samples[ring->write_pos] = unison_read_s16le(samples_s16le + i * 2);
        ring->write_pos = (ring->write_pos + 1) % ring->capacity;
        ring->available++;
    }
}

void unison_nds_audio_ring_pull(unison_nds_audio_ring *ring, int16_t *dest, size_t length) {
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
