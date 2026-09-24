#pragma once

#include "Engine/Kit.h"
#include "Engine/Movement/Vary.h"
#include "History.h"
#include "Library/Library.h"

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

    // Library ------------------------------------------------------------------
    // The library is shared by every Batida; call library().prepare() before
    // showing it (installs the factory files once).
    batida::Library& library() const { return *sharedLibrary; }

    // Browse: a run of loads of the same kind into the same place is one undo
    // step, so trying 30 sounds and pressing Undo returns to where you started.
    enum class LoadMode { Step, Browse };

    batida::SoundPreset captureSound (int voice) const;
    batida::KitPreset captureKit() const;
    batida::PatternPreset capturePattern (int pattern) const;
    batida::SetPreset captureSet() const;

    // Loading keeps the slot's Pan, Mute and Solo for a sound, Mute and Solo
    // for a kit; a set brings everything. `from` is remembered for Save.
    void applySound (int voice, const batida::SoundPreset& sound, const juce::File& from, LoadMode mode = LoadMode::Step);
    void applyKit (const batida::KitPreset& kit, const juce::File& from, LoadMode mode = LoadMode::Step);
    void applyPattern (int pattern, const batida::PatternPreset& preset, const juce::File& from, LoadMode mode = LoadMode::Step);
    void applySet (const batida::SetPreset& set, const juce::File& from, LoadMode mode = LoadMode::Step);

    // Any library file: a sound goes to `voice`, a pattern to `pattern`.
    bool loadPresetFile (const juce::File& file, int voice, int pattern, LoadMode mode, juce::String* error = nullptr);
    bool browseSample (int voice, const juce::File& file); // from the browser: merged undo

    void initSound (int voice); // a plain starting sound
    void initKit();             // the default kit
    void initPattern (int pattern);
    void initAll();             // like a new Batida

    // Writes the file and remembers it (Save then writes there again). With
    // collectSamples, the samples are copied into "<name> Samples" next to it.
    bool saveSound (int voice, const juce::File& file, const batida::PresetInfo& info, bool collectSamples, juce::String* error = nullptr);
    bool saveKit (const juce::File& file, const batida::PresetInfo& info, bool collectSamples, juce::String* error = nullptr);
    bool savePattern (int pattern, const juce::File& file, const batida::PresetInfo& info, juce::String* error = nullptr);
    bool saveSet (const juce::File& file, const batida::PresetInfo& info, bool collectSamples, juce::String* error = nullptr);

    // Where the current kit, sounds and patterns came from (saved with the project).
    struct Origins
    {
        juce::String kitName { "Neutral" }, setName;
        juce::File kitFile, setFile;
        std::array<juce::File, batida::kNumVoices> soundFiles;
        std::array<juce::String, batida::kNumPatterns> patternNames;
        std::array<juce::File, batida::kNumPatterns> patternFiles;
    };
    const Origins& getOrigins() const { return origins; }
    int getOriginsVersion() const { return originsVersion; }
    juce::String getPatternName (int pattern) const; // its file's name, or "Pattern 3"

    // Missing samples.
    int numMissingSamples() const;
    bool relinkSample (int voice, const juce::File& file);                 // one undo step
    int relinkFound (const std::vector<std::pair<int, juce::File>>& found); // one undo step for all
    int autoRelink();                                                      // searches the known folders
    juce::int64 missingSampleBytes (int voice) const { return sampleBytes[(size_t) voice]; }
    bool collectSamples (const juce::File& folder, juce::String* error = nullptr); // the project's samples

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

    // The output's peak level per channel since the last call (for the meter).
    std::array<float, 2> takeOutputPeaks();

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
    std::array<std::atomic<float>, 2> outputPeaks {};
    std::array<juce::String, batida::kNumVoices> voiceNames;
    std::atomic<int> namesVersion { 0 };

    HistorySnapshot snapshot (bool withParameters, bool withSamples) const;
    void restore (const HistorySnapshot& s);
    juce::int64 historyStamp() const;
    void writeVoiceParameters (int voice, const batida::VoiceParams& values);
    void writeGlobalParameter (int param, float value);
    void setSample (int voice, const batida::SampleRef& ref);
    batida::SampleRef sampleRefFor (int voice) const;
    void setNameQuietly (int voice, const juce::String& name);
    void beginLoadStep (const juce::String& key, LoadMode mode);
    void endLoadStep (const juce::String& key, LoadMode mode);
    std::unique_ptr<juce::XmlElement> originsToXml() const;
    void originsFromXml (const juce::XmlElement* xml);
    bool collectInto (batida::SampleRef& ref, const juce::File& presetFile, const juce::String& name, juce::String* error);
    void presetSaved (const juce::File& file);

    juce::SharedResourcePointer<batida::Library> sharedLibrary;
    Origins origins;
    int originsVersion = 0;
    std::array<juce::int64, batida::kNumVoices> sampleBytes {}; // sizes of the samples, for finding them again
    juce::String browseKey;
    juce::int64 browseStamp = 0;
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
