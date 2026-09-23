#pragma once

#include "VoicePages.h"

// Draws and edits one modulator's shape: click adds a point, drag moves it,
// double-click deletes it, Option-drag on a segment bends its curve. A line
// shows where the modulator is now.
class ShapeEditor final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    ShapeEditor (BatidaProcessor& p, int mod);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    void timerCallback() override { repaint(); }
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (6.0f); }
    juce::Point<float> toScreen (float x, float y) const;
    int pointAt (juce::Point<float> p) const;
    int segmentAt (float x) const;

    BatidaProcessor& proc;
    const int mod;
    int dragPoint = -1, curveSegment = -1;
    float curveStart = 0.0f;
};

// The targets of one modulator: name, depth, remove; and an Add menu.
class TargetList final : public juce::Component, private juce::Timer
{
public:
    TargetList (BatidaProcessor& p, int mod);

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;
    void rebuild();
    void showAddMenu();

    BatidaProcessor& proc;
    const int mod;
    juce::TextButton addButton { "Add target..." };
    std::array<juce::Label, batida::kMaxModTargets> names;
    std::array<juce::Slider, batida::kMaxModTargets> depths;
    std::array<juce::TextButton, batida::kMaxModTargets> removeButtons;
    std::array<int, batida::kMaxModTargets> slotOf {}; // row → target slot
    int rows = 0, shownVersion = -1, shownNames = -1;
};

class ModPanel final : public VoicePage, private juce::Timer
{
public:
    ModPanel (BatidaProcessor& p, int mod);
    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    const int mod;
    ParamControl *syncRate = nullptr, *freeRate = nullptr, *trigger = nullptr;
    juce::Label title;
    juce::TextButton shapeButton { "Shape..." };
    ShapeEditor shape;
    TargetList targets;
    int rateSection, feelSection;
};

// The MOD view: both modulators side by side.
class ModPage final : public juce::Component
{
public:
    explicit ModPage (BatidaProcessor& p);
    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    ModPanel one, two;
};
