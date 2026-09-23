#pragma once

#include "BrowseStrip.h"
#include "DraggableNumber.h"
#include "PatternGrid.h"

// The sequencer page: transport strip (pattern, length, run mode, start
// quantise, latch, play, sync, tempo, swing), lane selector, the 64-step page
// map, and the grid, one bar at a time.
class SeqPage final : public juce::Component, private juce::Timer
{
public:
    explicit SeqPage (BatidaProcessor& p);
    ~SeqPage() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    BrowseStrip patternStrip; // the editor wires it to the library

private:
    void timerCallback() override;
    void setLane (PatternGrid::Lane lane);

    BatidaProcessor& proc;
    PatternGrid grid;
    PageMap pageMap;

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    juce::Label patternLabel, lengthLabel, runLabel, quantiseLabel, tempoLabel, swingLabel, laneLabel, hint;
    juce::ComboBox patternBox, runBox, quantiseBox;
    juce::Slider lengthSlider;
    juce::ToggleButton latchButton { "Latch" }, playButton { "Play" }, syncButton { "Sync" };
    DraggableNumber tempo, swing;
    juce::TextButton copyButton { "Copy" }, pasteButton { "Paste" }, clearButton { "Clear" };
    std::array<juce::TextButton, 5> laneButtons;
    std::unique_ptr<ComboAttachment> patternAttachment, runAttachment, quantiseAttachment;
    std::unique_ptr<ButtonAttachment> latchAttachment, playAttachment, syncAttachment;
};
