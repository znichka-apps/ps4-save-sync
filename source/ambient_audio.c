#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ambient_audio.h"

static uint16_t le16(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int16_t *ambient_audio_load(const char *path, size_t *sample_count)
{
    unsigned char header[44];
    int16_t *samples = NULL;
    FILE *file;
    uint32_t bytes;
    if (sample_count) *sample_count = 0;
    if (!path || !sample_count || !(file = fopen(path, "rb"))) return NULL;
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVEfmt ", 8) ||
        le32(header + 16) != 16 || le16(header + 20) != 1 ||
        le16(header + 22) != 1 || le32(header + 24) != 48000 ||
        le32(header + 28) != 96000 || le16(header + 32) != 2 ||
        le16(header + 34) != 16 || memcmp(header + 36, "data", 4)) goto done;
    bytes = le32(header + 40);
    if (!bytes || bytes % 2 || bytes > 48000u * 60u * 2u ||
        le32(header + 4) != bytes + 36u) goto done;
    samples = malloc(bytes);
    if (!samples) goto done;
    if (fread(samples, 1, bytes, file) != bytes || fgetc(file) != EOF) {
        free(samples);
        samples = NULL;
        goto done;
    }
    *sample_count = bytes / 2;
done:
    fclose(file);
    return samples;
}

void ambient_audio_stereo(const int16_t *mono, size_t count, size_t *cursor,
    int16_t *stereo, size_t frames)
{
    if (!mono || !count || !cursor || !stereo) return;
    for (size_t i = 0; i < frames; i++) {
        int16_t sample = mono[*cursor];
        stereo[i * 2] = stereo[i * 2 + 1] = sample;
        if (++*cursor == count) *cursor = 0;
    }
}
