#pragma once

#include "Chain/KitChain.h"
#include "MidiRouter.h"
#include "SampleData.h"
#include "Voice.h"

namespace batida
{

// The 8 voices, split into dry and wet buses by each voice's Chain amount; the
// wet bus runs through the kit chain, the dry bus is added back, then the
// master level and the safety clipper.
class Kit
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // Call once per block, before process().
    void setParameters (const KitParams& params);

    // Clears the buffer and renders into it (2 channels, or 1 for a mono mix).
    // `sidechain` (optional, 1-2 channels, same length) keys the compressor.
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi,
                  const juce::AudioBuffer<float>* sidechain = nullptr);

    int getLatencySamples() const { return chain.getLatencySamples(); }
    float getGainReductionDb() const { return chain.getGainReductionDb(); }

    // Direct triggering, used by the editor's audition buttons and by tests.
    void noteOn (int voice, int key, float velocity) { voices[(size_t) voice].noteOn (key, velocity); }
    void noteOff (int voice, int key) { voices[(size_t) voice].noteOff (key); }

    SampleSlot& sampleSlot (int voice) { return slots[(size_t) voice]; }
    const Voice& getVoice (int voice) const { return voices[(size_t) voice]; }

private:
    void handle (const juce::MidiMessage& message);
    void renderSegment (int start, int numSamples, juce::AudioBuffer<float>& buffer,
                        const juce::AudioBuffer<float>* sidechain);

    std::array<Voice, kNumVoices> voices;
    std::array<SampleSlot, kNumVoices> slots;
    juce::AudioBuffer<float> scratch; // dry L/R, wet L/R
    KitChain chain;
    bool safetyClip = true;
    MidiMode midiMode = MidiMode::DrumMap;
    int keysVoice = 0;
    Smoother master;
    float masterTarget = 1.0f;
};

} // namespace batida
