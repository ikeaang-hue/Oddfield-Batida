#pragma once

#include "Engine/Kit.h"
#include "Engine/Movement/Vary.h"
#include "History.h"

#include <map>

#include <juce_audio_processors/juce_audio_processors.h>

class BatidaProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    BatidaProcessor();
    ~BatidaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; } // ringing voices after the last note

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Editor interface (message thread) ------------------------------------
    juce::AudioProcessorValueTreeState& getState() { return state; }
    batida::SampleSlot& sampleSlot (int voice) { return kit.sampleSlot (voice); }
    batida::PatternStore& patterns() { return kit.patternStore(); }
    const batida::Sequencer& sequencer() const { return kit.getSequencer(); }
    const batida::Kit& getKit() const { return kit; }

    // The pattern the grid shows: the one playing, or the Pattern parameter.
    int displayPattern() const;
    void setPatternParameter (int pattern);
    juce::AudioFormatManager& getFormats() { return formats; }

    // Loads a file into a voice. A voice on FM switches to Sample so the new
    // sound is heard straight away.
    bool loadSample (int voice, const juce::File& file);
    void clearSample (int voice);

    void audition (int voice, bool on);

    // Voice names (saved with the project; empty = the default name).
    juce::String getVoiceName (int voice) const;
    void setVoiceName (int voice, const juce::String& name);
    int getNamesVersion() const { return namesVersion.load(); }

    // Swaps two slots: every setting, the sample, the name, mute/solo and the
    // track in all 16 patterns move together; each slot keeps its MIDI note.
    void swapVoices (int a, int b);

    // Undo/redo (patterns, names, big actions). Call beginUndoStep before a
    // change; withParameters/withSamples for actions that change those too.
    void beginUndoStep (bool withParameters = false, bool withSamples = false);
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const { return history.canRedo(); }

    // Movement ---------------------------------------------------------------
    batida::SnapshotStore<batida::MovementData>& movement() { return kit.movementStore(); }

    // What a host parameter ID refers to, for modulation.
    struct ParamRef { bool valid = false, global = false; int voice = 0, param = 0; };
    ParamRef refFor (const juce::String& paramID) const;
    bool isModulatable (const juce::String& paramID) const;
    bool isModTarget (int mod, const juce::String& paramID) const;
    void toggleModTarget (int mod, const juce::String& paramID);

    // Live modulation of a knob, in its own units: the current value and the
    // range the modulators can reach. Empty when nothing modulates it.
    struct ModDisplay { double live, low, high; };
    std::optional<ModDisplay> modDisplayFor (const juce::String& paramID) const;

    // Scenes: store the chain into a slot; recall writes it back (undoable).
    void storeScene (int scene);
    void recallScene (int scene);

    // Vary ---------------------------------------------------------------------
    void startVary (int voice, float amount, batida::VaryDirection direction, bool lockSource, bool lockFx, bool lockEnvelopes);
    bool isVarying() const { return varyBusy.load(); }
    const std::vector<batida::VaryCandidate>& varyCandidates() const { return candidates; }
    int varyVoice() const { return candidatesVoice; }
    const batida::VoiceParams& varyBase() const { return candidatesBase; } // the sound the suggestions came from
    int getVaryVersion() const { return varyVersion.load(); }
    void previewCandidate (int index); // -1 = back to the original
    int previewedCandidate() const { return previewIndex; }
    void keepCandidate();
    static batida::VoiceParams withLiveMix (const batida::VaryCandidate& c, const batida::VoiceParams& live);
    void setKeysVoice (int voice); // what Chromatic mode plays; follows the editor selection
    batida::VoiceParams readVoiceParams (int voice) const;
    std::array<float, batida::kNumGlobalParams> readGlobalParams() const;
    float getGainReductionDb() const { return kit.getGainReductionDb(); }

    std::atomic<int> selectedVoice { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;

    juce::AudioFormatManager formats;
    juce::AudioProcessorValueTreeState state;
    batida::Kit kit;
    batida::KitParams params;
    juce::AudioBuffer<float> sidechainCopy;

    std::array<std::array<std::atomic<float>*, batida::kNumVoiceParams>, batida::kNumVoices> voiceRaw {};
    std::array<std::atomic<float>*, batida::kNumGlobalParams> globalRaw {};
    std::atomic<uint32_t> auditionOn { 0 }, auditionOff { 0 };
    std::array<juce::String, batida::kNumVoices> voiceNames;
    std::atomic<int> namesVersion { 0 };

    HistorySnapshot snapshot (bool withParameters, bool withSamples) const;
    void restore (const HistorySnapshot& s);
    juce::int64 historyStamp() const;
    void writeVoiceParameters (int voice, const batida::VoiceParams& values);
    History history;
    int actionCounter = 0;
    std::map<juce::String, ParamRef> paramRefs;

    // Vary: a background job, and a preview that overrides one voice's
    // settings on the audio thread without touching the host parameters.
    struct Preview { bool active = false; int voice = 0; batida::VoiceParams params; };
    batida::SnapshotStore<Preview> preview;
    std::vector<batida::VaryCandidate> candidates;
    batida::VoiceParams candidatesBase;
    int candidatesVoice = -1, previewIndex = -1;
    std::atomic<int> varyVersion { 0 };
    std::atomic<bool> varyBusy { false };
    juce::ThreadPool varyPool { 1 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (BatidaProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BatidaProcessor)
};
