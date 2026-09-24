#pragma once

#include "ParamControl.h"
#include "Widgets.h"
#include "Plugin/PluginProcessor.h"

// The XY pad: X = character (warm → aggressive → digital), Y = heat (clean →
// destroyed). A white square is where you put it; a lime square is where the
// chain really is when a step lock or a modulator moves it, joined by a dotted
// line. Drags are relative (the puck never jumps); Shift locks to one axis;
// Option-click glides the puck to that spot; double-click glides back to the
// default. A one-second trail follows the effective position.
class XyPad final : public juce::Component
{
public:
    explicit XyPad (BatidaProcessor& p);

    std::function<void (float, float)> onMove; // after a drag moves the pad
    std::function<void()> onGestureStart;
    juce::Point<float> getEffective() const { return shownEffective.x < 0.0f ? targetEffective() : shownEffective; }
    juce::Point<float> getPosition() const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void frame(); // 60 fps while anything moves
    void set (float x, float y);
    void glideTo (juce::Point<float> target);
    juce::Point<float> targetEffective() const;

    BatidaProcessor& proc;
    juce::RangedAudioParameter* x;
    juce::RangedAudioParameter* y;
    juce::VBlankAttachment vblank;

    struct TrailPoint { juce::Point<float> p; double t; };
    std::vector<TrailPoint> trail;
    juce::Point<float> shownEffective { -1.0f, -1.0f }, shownSet { -1.0f, -1.0f };
    std::optional<juce::Point<float>> hover;
    bool grabbing = false;
    float grabScale = 0.0f;
    juce::Point<float> grabStart;
    juce::Point<float> mouseStart;
    struct Glide { juce::Point<float> from, to; double start; };
    std::optional<Glide> glide;
    double lastFrame = 0.0;
};

// One scene slot: its letter and a mini pad with the stored XY position.
class SceneTile final : public juce::Component
{
public:
    SceneTile (BatidaProcessor& p, int scene);
    std::function<void()> onClick;
    void setArmed (bool storeArmed);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    BatidaProcessor& proc;
    const int scene;
    bool armed = false;
};

// KIT: the XY pad with its zones and XY REC, the chain drawn as a signal path
// (01 Dynamics, 02 Distortion, 03 EQ, 04 Output, each with XY follow), and the
// scenes A–D with the morph.
class KitPage final : public juce::Component, private juce::Timer
{
public:
    explicit KitPage (BatidaProcessor& p);
    ~KitPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    ParamControl& add (int param, const juce::String& label);

    BatidaProcessor& proc;
    XyPad pad;
    juce::OwnedArray<ParamControl> controls;
    std::array<std::vector<ParamControl*>, 4> stages;
    std::array<ParamControl*, 4> follow {};
    ParamControl *compAmount, *compAttack, *drive, *type, *lowKeep, *exciter, *tone, *eqHigh;
    ParamControl *morphOn, *morphFrom, *morphTo, *morph;
    juce::ToggleButton xyRec { "XY REC" }, store { "STORE" };
    juce::OwnedArray<SceneTile> tiles;
    std::array<juce::Rectangle<int>, 4> stageBounds;
    juce::Rectangle<int> scenesBounds, flowBounds, zonesBounds, readoutBounds;
    float grDb = 0.0f;
    int shownMovement = -1, shownZone = -1;
    juce::Point<float> shownSet { -1.0f, -1.0f };
};
