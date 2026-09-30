#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <vector>

// Measurements of rendered audio, for when a render can't be listened to: the
// preset factory judges its candidates by them, and `batida analyze` prints
// them for any file (SPEC §13).

namespace batida
{

// One hit, from its mono sum.
struct HitMeasures
{
    float peakDb = -120.0f, loudnessDb = -120.0f; // loudness: K-weighted, the loudest 50 ms
    float lengthMs = 0.0f;                        // until -60 dB below the peak
    float centroidHz = 0.0f;                      // spectral centroid
    float low = 0.0f, high = 0.0f;                // share of energy below 150 Hz, above 6 kHz
    float flatness = 0.0f;                        // 0 tonal .. 1 white noise (200 Hz..16 kHz)
    float inharmonic = 0.0f;                      // 0 harmonic .. 1 clangorous (tonal parts)
    float pitchHz = 0.0f;                         // after the attack, from zero crossings (tonal sounds)
    float crestDb = 0.0f;                         // peak over loudness: how spiky the attack is
    bool finite = true;
};

HitMeasures measureHit (const float* mono, int numSamples, double sampleRate);

// Any audio: a hit, a beat or a file.
struct AudioMeasures
{
    double sampleRate = 48000.0;
    int channels = 0;
    double seconds = 0.0;
    bool finite = true;

    float peakDb = -120.0f;      // sample peak, dBFS
    float truePeakDb = -120.0f;  // 4x oversampled peak, dBTP
    int clipped = 0;             // samples at or over full scale
    float loudnessLufs = -120.0f; // integrated, BS.1770 (gated)
    float loudestLufs = -120.0f;  // the loudest 400 ms (momentary)
    float widthPercent = 0.0f;   // side energy over total: 0 mono .. 100 out of phase

    HitMeasures sound;         // of the mono sum (length, tone, noise, pitch)

    static constexpr int kBands = 8;
    static constexpr std::array<float, kBands> kBandHz { 63, 125, 250, 500, 1000, 2000, 4000, 8000 };
    std::array<float, kBands> bandsDb {}; // octave bands, dB relative to the whole spectrum

    struct Onset { double seconds; float strength; }; // strength 0..1 of the strongest
    std::vector<Onset> onsets;

    bool isSilent() const { return peakDb < -80.0f; }
};

AudioMeasures measureAudio (const juce::AudioBuffer<float>& audio, double sampleRate);

// A pitch as a MIDI note number (69 = A 440 Hz), and its name with C3 = 60,
// as Batida and Logic name notes.
int noteNumberFor (float hz);
juce::String noteName (int note);

} // namespace batida
