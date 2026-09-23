#pragma once

#include "ParamControl.h"
#include "Plugin/PluginProcessor.h"

// Base for the per-voice tabs: titled sections of parameter controls that
// re-bind to whichever voice is selected.
class VoicePage : public juce::Component
{
public:
    explicit VoicePage (BatidaProcessor& p) : proc (p) {}

    virtual void bindVoice (int newVoice);
    void paint (juce::Graphics& g) override;

protected:
    int addSection (const juce::String& title);
    ParamControl& add (int section, int param, const juce::String& label = {});
    void setSectionTitle (int section, const juce::String& title) { sections[(size_t) section].title = title; }

    // Lays sections out left to right from the top-left of `area`; returns the
    // height used. Controls in a section wrap after `perRow`.
    int layoutRow (juce::Rectangle<int> area, std::initializer_list<int> sectionIndices, int perRow = 100);
    static int sectionWidth (int numControls) { return numControls * ParamControl::kWidth + 12; }

    BatidaProcessor& proc;
    int voice = 0;

private:
    struct Section
    {
        juce::String title;
        std::vector<ParamControl*> controls;
        juce::Rectangle<int> bounds;
    };

    std::vector<Section> sections;
    std::vector<std::pair<std::unique_ptr<ParamControl>, int>> controls; // control, voice param index
};

// Draws the loaded sample with start/end and loop markers.
class WaveformView final : public juce::Component
{
public:
    explicit WaveformView (BatidaProcessor& p) : proc (p) {}

    void setVoice (int v) { voice = v; repaint(); }
    void paint (juce::Graphics& g) override;

private:
    BatidaProcessor& proc;
    int voice = 0;
};

class SourcePage final : public VoicePage, private juce::Timer
{
public:
    explicit SourcePage (BatidaProcessor& p);

    void bindVoice (int newVoice) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshStatus();

    int voiceSection, sampleSection, loopSection;
    juce::TextButton loadButton { "Load..." }, clearButton { "Clear" };
    juce::Label fileLabel;
    WaveformView waveform;
    std::unique_ptr<juce::FileChooser> chooser;

    int lastVersion = -1;
    std::array<float, 5> lastMarkers {};
};

class ChainPage final : public VoicePage
{
public:
    explicit ChainPage (BatidaProcessor& p);
    void resized() override;

private:
    int punchSection, driveSection, filterSection;
    juce::Label note;
};

class EnvelopePage final : public VoicePage
{
public:
    explicit EnvelopePage (BatidaProcessor& p);
    void resized() override;

private:
    int playSection, ampSection, pitchSection;
    juce::Label note;
};
