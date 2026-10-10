#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "ambient_audio.h"

int main(void)
{
    size_t count = 0, cursor;
    int16_t *music = ambient_audio_load("assets/audio/ambient.wav", &count);
    assert(music && count == 48000u * 16u);
    int maximum = 0;
    long long windows[4] = {0};
    for (size_t i = 0; i < count; i++) {
        int amplitude = abs((int)music[i]);
        if (amplitude > maximum) maximum = amplitude;
        windows[i / (count / 4)] += amplitude;
    }
    assert(maximum > 12000 && maximum < 25000);
    for (int i = 0; i < 4; i++) assert(windows[i] > 100000000);
    assert(abs((int)music[0]) < 100 && abs((int)music[count - 1]) < 100);
    cursor = count - 2;
    int16_t stereo[8] = {0};
    ambient_audio_stereo(music, count, &cursor, stereo, 4);
    assert(stereo[0] == music[count - 2] && stereo[1] == stereo[0]);
    assert(stereo[2] == music[count - 1] && stereo[3] == stereo[2]);
    assert(stereo[4] == music[0] && stereo[5] == stereo[4]);
    assert(stereo[6] == music[1] && stereo[7] == stereo[6] && cursor == 2);
    free(music);
    puts("Ambient WAV format, level, loop and stereo playback tests passed.");
    return 0;
}
