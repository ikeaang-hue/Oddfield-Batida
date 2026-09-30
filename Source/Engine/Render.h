#pragma once

#include "Kit.h"

#include <memory>

namespace batida
{

// Offline renders of the kit as it is, for resampling (SPEC §3): a private
// Kit with the same settings, samples (shared, not copied), modulators and
// patterns, run faster than real time on any thread.
struct RenderRequest
{
    enum class Mode { Sound, Pattern };
    Mode mode = Mode::Sound;
    KitParams params;
    MovementData movement;
    PatternBank bank;
    std::array<std::shared_ptr<const SampleData>, kNumVoices> samples;
    double sampleRate = 48000.0;
    int voice = 0;       // Sound: the sound to hit
    int key = Voice::kBaseKey;   // Sound: the note (60 plays the sound at its own pitch)
    float velocity = 1.0f;       // Sound: 0..1
    int pattern = 0;     // Pattern: the pattern to play
    double bpm = 120.0;  // Pattern: its tempo
    int steps = 0;       // Pattern: 0 = one seamless loop; more = that many steps from the start, then the tail
};

// Sound: one hit through the chain, until it has rung out (at most 6 s).
// Pattern: one pass that loops seamlessly: the second of two passes, so the
// tails of the first pass's last hits are heard at its start. With `steps`,
// the pattern plays from silence for that many 16th steps, and the render
// goes on until the tails have rung out (at most 6 s more). Stereo.
juce::AudioBuffer<float> renderKit (const RenderRequest& request);

// The length in samples of one pass of a pattern at a tempo.
int patternPassSamples (const Pattern& pattern, double bpm, double sampleRate);

} // namespace batida
