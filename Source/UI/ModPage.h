#pragma once

#include "ParamControl.h"
#include "Widgets.h"
#include "Plugin/PluginProcessor.h"

// Draws and edits one modulator's shape: click adds a point, drag moves it,
// double-click deletes it, Option-drag on a segment bends its curve. The
// playhead shows where the modulator is and its value.
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
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (8.0f); }
    juce::Point<float> toScreen (float x, float y) const;
    int pointAt (juce::Point<float> p) const;
    int segmentAt (float x) const;

    BatidaProcessor& proc;
    const int mod;
    int dragPoint = -1, curveSegment = -1;
    float curveStart = 0.0f;
};

// A target's depth, −100%..+100%: a bipolar bar, dragged from where it is.
class DepthBar final : public juce::Component
{
public:
    std::function<void (float)> onChange;
    void setDepth (float d) { if (! juce::exactlyEqual (d, depth)) { depth = d; repaint(); } }
    void setLabel (const juce::String& n) { name = n; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { depth = 0.0f; if (onChange) onChange (0.0f); repaint(); }

private:
    juce::String name;
    float depth = 0.0f, start = 0.0f;
    bool dragging = false;
};

// One modulator: shape, timing, output and targets.
class ModPanel final : public juce::Component, private juce::Timer
{
public:
    ModPanel (BatidaProcessor& p, int mod);
    ~ModPanel() override;
    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;
    void rebuildTargets();
    void showAddMenu();
    ParamControl& add (int field, const juce::String& label);

    BatidaProcessor& proc;
    const int mod;
    ShapeEditor shape;
    juce::OwnedArray<ParamControl> controls;
    ParamControl *mode, *rate, *freeRate, *trigger, *polarity, *smooth, *humanise, *amount;
    TextLink shapes { juce::String::fromUTF8 ("SHAPES \xe2\x96\xbe"), true }, addTarget { "+ ADD TARGET", true };
    juce::OwnedArray<DepthBar> depths;
    juce::OwnedArray<TextLink> removes;
    std::array<int, batida::kMaxModTargets> slotOf {};
    int rows = 0, shownVersion = -1, shownNames = -1;
    juce::Rectangle<int> separator1, separator2, targetsHeader;
};

// MOD: both modulators side by side.
class ModPage final : public juce::Component
{
public:
    explicit ModPage (BatidaProcessor& p);
    void resized() override;

private:
    ModPanel one, two;
};
