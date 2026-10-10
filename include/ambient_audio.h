#ifndef AMBIENT_AUDIO_H
#define AMBIENT_AUDIO_H

#include <stddef.h>
#include <stdint.h>

/* Canonical 48 kHz, 16-bit mono PCM WAV. The caller owns the returned buffer. */
int16_t *ambient_audio_load(const char *path, size_t *sample_count);
void ambient_audio_stereo(const int16_t *mono, size_t count, size_t *cursor,
    int16_t *stereo, size_t frames);

#endif
