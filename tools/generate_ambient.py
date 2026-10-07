#!/usr/bin/env python3
"""Render this project's original 16-second ambient loop as PCM WAV.

No recordings or third-party samples are used. The notes, envelopes and tones
below are synthesized so the shipped file can be regenerated reproducibly.
"""
from array import array
from math import exp, pi, sin
from pathlib import Path
import sys
import wave

RATE = 48_000
SECONDS = 16
CHORDS = (
    (57, 60, 64, 71),  # A minor add9
    (53, 57, 60, 64),  # F major 7
    (48, 52, 55, 59),  # C major 7
    (55, 59, 62, 64),  # G major 6
)
ARPEGGIO = (0, 2, 1, 3)
DEST = Path(__file__).resolve().parents[1] / "assets/audio/ambient.wav"


def hz(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def sample(t):
    chord = CHORDS[int(t // 4)]
    local = t % 4
    # Each chord fades in and out; voices are gently detuned and change harmony.
    envelope = min(1.0, local / 0.55, (4.0 - local) / 0.75)
    pad = 0.0
    for note in chord:
        frequency = hz(note)
        pad += 0.115 * sin(2 * pi * frequency * t)
        pad += 0.032 * sin(2 * pi * frequency * 1.003 * t)
    root = 0.09 * sin(2 * pi * hz(chord[0] - 12) * t)

    beat = int(local)
    age = local - beat
    note = chord[ARPEGGIO[beat]] + 12
    frequency = hz(note)
    pluck = min(1.0, age * 18.0) * exp(-3.8 * age)
    bell = pluck * (0.25 * sin(2 * pi * frequency * age)
                    + 0.065 * sin(2 * pi * frequency * 2.01 * age))

    # Both ends of the complete file approach zero for a quiet loop seam.
    loop_fade = min(1.0, t / 0.08, (SECONDS - t) / 0.45)
    return (envelope * (pad + root) + bell) * loop_fade


def main():
    values = [sample(i / RATE) for i in range(RATE * SECONDS)]
    peak = max(abs(value) for value in values)
    pcm = array("h", (int(max(-1.0, min(1.0, value / peak * 0.58)) * 32767)
                      for value in values))
    if sys.byteorder != "little":
        pcm.byteswap()
    DEST.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(DEST), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(pcm.tobytes())
    print(f"Wrote {DEST} ({len(pcm)} samples, {peak:.3f} pre-normalized peak)")


if __name__ == "__main__":
    main()
