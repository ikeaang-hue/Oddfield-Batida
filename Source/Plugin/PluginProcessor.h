#pragma once

#include "Engine/Kit.h"

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
    juce::AudioFormatManager& getFormats() { return formats; }

    // Loads a file into a voice. A voice on FM switches to Sample so the new
    // sound is heard straight away.
    bool loadSample (int voice, const juce::File& file);
    void clearSample (int voice);

    void audition (int voice, bool on);
    void setKeysVoice (int voice); // what Chromatic mode plays; follows the editor selection
    batida::VoiceParams readVoiceParams (int voice) const;

    std::atomic<int> selectedVoice { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;

    juce::AudioFormatManager formats;
    juce::AudioProcessorValueTreeState state;
    batida::Kit kit;
    batida::KitParams params;

    std::array<std::array<std::atomic<float>*, batida::kNumVoiceParams>, batida::kNumVoices> voiceRaw {};
    std::array<std::atomic<float>*, batida::kNumGlobalParams> globalRaw {};
    std::atomic<uint32_t> auditionOn { 0 }, auditionOff { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BatidaProcessor)
};
