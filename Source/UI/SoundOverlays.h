#pragma once

#include "Graphs.h"
#include "ParamControl.h"
#include "Widgets.h"
#include "Plugin/PluginProcessor.h"

// Panels that open over the SOUND page. The page behind dims; the panel rises
// 8 px and fades in (120 ms), and closes faster (80 ms).
class Overlay : public juce::Component
{
public:
    Overlay();
    void open();
    void close();
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

protected:
    juce::Rectangle<int> panel;     // set by the subclass's resized()
    float shown = 0.0f;              // 0..1, for the fade and rise
    TextLink closeButton { juce::String::fromUTF8 ("CLOSE \xc3\x97"), true };
    void drawHeader (juce::Graphics& g, const juce::String& tag, const juce::String& title);

private:
    void animate();
    std::unique_ptr<juce::VBlankAttachment> vblank;
    double start = 0.0;
    bool opening = false;
};

// One Vary suggestion: waveform and what changed. It plays while held.
class VaryTile final : public juce::Component
{
public:
    VaryTile();
    void set (const juce::String& name, const juce::String& note, std::vector<float> peaks, bool original);
    void setKept (bool kept);
    std::function<void()> onPress, onRelease;
    bool isHeld() const { return held; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    PeaksView peaks;
    juce::VBlankAttachment vblank;
    juce::String name, note;
    bool held = false, original = false, kept = false, available = false;
    double heldSince = 0.0;
};

// Vary: amount, direction, sections to keep, and the original plus up to 4
// suggestions. Hold one to hear it; KEEP makes it the sound (one undo step).
class VaryOverlay final : public Overlay, private juce::Timer
{
public:
    explicit VaryOverlay (BatidaProcessor& p);
    ~VaryOverlay() override;
    void setVoice (int voice);
    void startVary();
    std::function<void()> onSave;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh();

    BatidaProcessor& proc;
    int voice = 0, shownVersion = -1;
    Segmented amount { { "Subtle", "Medium", "Far" }, 1 };
    Segmented direction { { "Any", "Brighter", "Darker", "Shorter", "Longer", "Tonal", "Noisy", "Cleaner", "Dirtier" } };
    juce::ToggleButton lockSource { "Source" }, lockFx { "FX" }, lockEnvelopes { "Envelopes" };
    SolidButton again { "VARY AGAIN", theme::lime };
    SolidButton keep { "KEEP", theme::ink };
    TextLink back { "BACK", true }, save { juce::String::fromUTF8 ("SAVE TO LIBRARY\xe2\x80\xa6"), true };
    juce::OwnedArray<VaryTile> tiles; // original + 4
    juce::String status;
};

// Every control of the four FM operators, with their envelopes; the macros
// along the bottom (their pushes show in lime above).
class OperatorEditor final : public Overlay, private juce::Timer
{
public:
    explicit OperatorEditor (BatidaProcessor& p);
    ~OperatorEditor() override;
    void bindVoice (int voice);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    BatidaProcessor& proc;
    int voice = 0;
    juce::OwnedArray<ParamControl> controls;
    std::vector<std::pair<ParamControl*, int>> bound;
    ParamControl *algorithm, *pitch, *feedback;
    std::array<std::array<ParamControl*, 10>, batida::kNumOps> opControls {};
    std::array<ParamControl*, 4> macros {};
    juce::OwnedArray<WaveGlyph> glyphs;
    juce::OwnedArray<MiniEnvelope> envs;
    std::array<juce::Rectangle<int>, batida::kNumOps> columns;
    std::array<bool, batida::kNumOps> carriers {}, active {};
};
