#pragma once

#include "Chain/KitChain.h"
#include "MidiRouter.h"
#include "Movement/Modulators.h"
#include "SnapshotStore.h"
#include "Sequencer/PatternStore.h"
#include "Sequencer/Sequencer.h"
#include "SampleData.h"
#include "Voice.h"

namespace batida
{

// The 8 voices, split into dry and wet buses by each voice's Chain amount; the
// wet bus runs through the kit chain, the dry bus is added back, then the
// master level and the safety clipper. The sequencer's hits and live MIDI go
// into the voices the same way; pattern keys (C3-D#4) drive the sequencer.
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
                  const juce::AudioBuffer<float>* sidechain = nullptr, const Transport& transport = {});

    int getLatencySamples() const { return chain.getLatencySamples(); }
    float getGainReductionDb() const { return chain.getGainReductionDb(); }

    // Direct triggering, used by the editor's audition buttons and by tests.
    void noteOn (int voice, int key, float velocity, int slice = -1) { voices[(size_t) voice].noteOn (key, velocity, slice); }
    void noteOff (int voice, int key) { voices[(size_t) voice].noteOff (key); }

    SampleSlot& sampleSlot (int voice) { return slots[(size_t) voice]; }
    PatternStore& patternStore() { return patterns; }
    int getPatternVersion() const { return patterns.getVersion(); }
    SnapshotStore<MovementData>& movementStore() { return movement; }
    const SnapshotStore<MovementData>& movementStore() const { return movement; }
    const Modulators& getModulators() const { return mods; }
    float getCurrentGlobal (int param) const { return current.global[(size_t) param]; } // after morph + modulation

    // Blocks of this many samples between modulation updates.
    static constexpr int kControlSamples = 32;
    const Sequencer& getSequencer() const { return sequencer; }

    // The XY position the chain is using right now (a step lock or the pad).
    bool isXyLocked() const { return uiXyLocked.load(); }
    float getLockX() const { return uiLockX.load(); }
    float getLockY() const { return uiLockY.load(); }

    // The host's tempo, when it's playing and Sync is on (for the tempo display).
    bool isFollowingHost() const { return uiFollowingHost.load(); }
    double getHostBpm() const { return uiHostBpm.load(); }
    const Voice& getVoice (int voice) const { return voices[(size_t) voice]; }

private:
    void handle (const juce::MidiMessage& message);
    void handle (const SeqEvent& e);
    void applyParams (bool modulatedOnly);
    void computeParams();
    void tick (int offset); // one control step: advance the modulators, apply them
    bool isPatternKey (const juce::MidiMessage& message) const;
    void applyChainSettings();
    void renderSegment (int start, int numSamples, juce::AudioBuffer<float>& buffer,
                        const juce::AudioBuffer<float>* sidechain);

    std::array<Voice, kNumVoices> voices;
    std::array<SampleSlot, kNumVoices> slots;
    juce::AudioBuffer<float> scratch; // dry L/R, wet L/R
    KitChain chain;
    bool safetyClip = true;
    Sequencer sequencer;
    PatternStore patterns;
    SnapshotStore<MovementData> movement { defaultMovement() };
    const MovementData* movementData = nullptr;
    Modulators mods;
    KitParams host, current;  // from the host; after scene morph and modulation
    std::array<bool, kNumVoices> voiceModulated {};
    bool globalsModulated = false, anyTargets = false;
    int controlCountdown = 0;
    double sampleRateHz = 48000.0, clockPpq = 0.0, clockPpqPerSample = 0.0, internalClock = 0.0;
    SeqSettings seqSettings;
    std::vector<SeqEvent> seqEvents;
    std::array<float, kNumGlobalParams> globals {};
    bool xyLocked = false;
    float lockX = 0.5f, lockY = 0.0f;
    std::atomic<bool> uiXyLocked { false };
    std::atomic<float> uiLockX { 0.5f }, uiLockY { 0.0f };
    std::atomic<bool> uiFollowingHost { false };
    std::atomic<double> uiHostBpm { 120.0 };

    MidiMode midiMode = MidiMode::DrumMap;
    int keysVoice = 0;
    Smoother master;
    float masterTarget = 1.0f;
};

} // namespace batida
