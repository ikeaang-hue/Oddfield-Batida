#pragma once

#include "VoicePages.h"

// The XY pad: X = character (warm → aggressive → digital), Y = heat
// (clean → destroyed). Drag to move; double-click to return to the default.
// Writes the host parameters xy_x / xy_y, so it records as automation.
class XyPad final : public juce::Component, private juce::Timer
{
public:
    explicit XyPad (juce::AudioProcessorValueTreeState& state);

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
};

// The main panel for the whole kit (opens first): the XY pad, the four chain
// stages with their effective-value markers and Follow XY toggles, and a
// Chain amount per voice.
class KitPage final : public VoicePage, private juce::Timer
{
public:
    explicit KitPage (BatidaProcessor& p);
    void resized() override;

private:
    void timerCallback() override;

    XyPad pad;
    int dynamicsSection, distortionSection, eqSection, outputSection, chainSection;

    ParamControl *compAmount, *compAttack, *drive, *type, *lowKeep, *exciter, *tone, *eqHigh;
};
