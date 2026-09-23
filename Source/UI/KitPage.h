#pragma once

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
    std::function<void()> onGestureStart;
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

// The main panel for the whole kit (opens first): the XY pad (with XY Rec,
// which records pad moves into the playing pattern's steps), the four chain
// stages with their effective-value markers and Follow XY toggles, and the
// Chain amount per voice.
class KitPage final : public VoicePage, private juce::Timer
{
public:
    explicit KitPage (BatidaProcessor& p);
    ~KitPage() override;
    void resized() override;

private:
    void timerCallback() override;

    void refreshScenes();

    XyPad pad;
    juce::ToggleButton xyRecButton { "XY Rec" };
    juce::TextButton storeButton { "Store" };
    std::array<juce::TextButton, batida::kNumScenes> sceneButtons;
    int shownMovement = -1;
    int dynamicsSection, distortionSection, eqSection, outputSection, chainSection, morphSection;
    ParamControl *compAmount, *compAttack, *drive, *type, *lowKeep, *exciter, *tone, *eqHigh;
    std::array<ParamControl*, batida::kNumVoices> chainControls {};
    int shownNames = -1;
};
