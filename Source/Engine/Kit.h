#pragma once

#include "MidiRouter.h"
#include "SampleData.h"
#include "Voice.h"

namespace batida
{

// The 8 voices, mixed to stereo. The kit chain (phase 3) will sit between the
// voice mix and the master level.
class Kit
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // Call once per block, before process().
    void setParameters (const KitParams& params);

    // Clears the buffer and renders into it (2 channels, or 1 for a mono mix).
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi);

    // Direct triggering, used by the editor's audition buttons and by tests.
    void noteOn (int voice, int key, float velocity) { voices[(size_t) voice].noteOn (key, velocity); }
    void noteOff (int voice, int key) { voices[(size_t) voice].noteOff (key); }

    SampleSlot& sampleSlot (int voice) { return slots[(size_t) voice]; }
    const Voice& getVoice (int voice) const { return voices[(size_t) voice]; }

private:
    void handle (const juce::MidiMessage& message);
    void renderSegment (int start, int numSamples, juce::AudioBuffer<float>& buffer);

    std::array<Voice, kNumVoices> voices;
    std::array<SampleSlot, kNumVoices> slots;
    juce::AudioBuffer<float> scratch;
    MidiMode midiMode = MidiMode::DrumMap;
    Smoother master;
    float masterTarget = 1.0f;
};

} // namespace batida
