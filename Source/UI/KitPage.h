#pragma once

#include "PatternGrid.h"
#include "VoicePages.h"

// The XY pad: X = character (warm → aggressive → digital), Y = heat
// (clean → destroyed). Drag to move; double-click to return to the default.
// Writes the host parameters xy_x / xy_y, so it records as automation. A ring
// shows where a sequencer step's XY lock has moved the chain.
class XyPad final : public juce::Component, private juce::Timer
{
public:
    explicit XyPad (juce::AudioProcessorValueTreeState& state);

    std::function<void (float, float)> onMove; // after a drag moves the pad
    void setLock (std::optional<juce::Point<float>> lock);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> padArea() const;
    void setFromMouse (juce::Point<float> p);

    juce::RangedAudioParameter* x;
    juce::RangedAudioParameter* y;
    float shownX = -1.0f, shownY = -1.0f;
    std::optional<juce::Point<float>> lockPos;
};

// The main panel for the whole kit (opens first): the XY pad, the four chain
// stages with their effective-value markers and Follow XY toggles, the
// transport strip and the pattern grid (with a Chain amount per voice).
class KitPage final : public VoicePage, private juce::Timer
{
public:
    explicit KitPage (BatidaProcessor& p);
    ~KitPage() override;
    void resized() override;

private:
    void timerCallback() override;
    void setLane (PatternGrid::Lane lane, juce::TextButton& button);

    XyPad pad;
    PatternGrid grid;
    int dynamicsSection, distortionSection, eqSection, outputSection;
    ParamControl *compAmount, *compAttack, *drive, *type, *lowKeep, *exciter, *tone, *eqHigh;

    // Transport strip
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    juce::Label patternLabel, lengthLabel, runLabel, quantiseLabel, tempoLabel, swingLabel, hint;
    juce::ComboBox patternBox, runBox, quantiseBox;
    juce::Slider lengthSlider, tempoSlider, swingSlider;
    juce::ToggleButton latchButton { "Latch" }, playButton { "Play" }, xyRecButton { "XY Rec" };
    juce::TextButton copyButton { "Copy" }, pasteButton { "Paste" }, clearButton { "Clear" };
    std::array<juce::TextButton, 6> laneButtons;
    std::array<juce::TextButton, 2> pageButtons;
    std::unique_ptr<ComboAttachment> patternAttachment, runAttachment, quantiseAttachment;
    std::unique_ptr<ButtonAttachment> latchAttachment, playAttachment;
    std::unique_ptr<SliderAttachment> tempoAttachment, swingAttachment;
};
