#pragma once

#include "Pattern.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace batida
{

// A pattern as a standard MIDI file that any DAW takes, and that plays the
// same kit back from any DAW (SPEC §8):
//   - slots 1–8 are notes 36–43 on channel 10 (Batida's drum map, which
//     answers in both MIDI modes);
//   - timing is in beats (960 ticks each) with no tempo, so the region lands
//     on the grid at the song's tempo; swing is written in;
//   - ratchets are written out as the repeated notes;
//   - probability is rolled once (the same roll Batida's own playback makes);
//   - the XY lane becomes CC 16 (X) and CC 17 (Y); where a lock ends, the
//     CCs return to the pad's position.
// One pass of the pattern, as the real sequencer plays it.
struct MidiExportOptions
{
    float swing = 0.5f;
    float padX = 0.5f, padY = 0.0f; // where the XY lane lets go to
};

juce::MidiFile patternToMidi (const PatternBank& bank, int pattern, const MidiExportOptions& options = {});

constexpr int kMidiTicksPerBeat = 960;
constexpr int kMidiDrumChannel = 10;

} // namespace batida
