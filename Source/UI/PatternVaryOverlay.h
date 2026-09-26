#pragma once

#include "SoundOverlays.h"

// One pattern suggestion drawn as a small grid: white steps as in the
// original, lime where it changed, a faint box where a hit was taken out.
// It plays while held.
class PatternTile final : public juce::Component
{
public:
    void set (const juce::String& name, const juce::String& note, const batida::Pattern* pattern, const batida::Pattern& base, bool original);
    void setKept (bool kept);
    std::function<void()> onPress, onRelease;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::String name, note;
    std::optional<batida::Pattern> pattern;
    batida::Pattern base;
    bool held = false, original = false, kept = false;
};

// Vary for patterns, over the SEQ page: amount, direction, tracks to keep as
// they are, and the original plus up to 4 suggestions. Hold one to hear it
// (it plays even when the sequencer is stopped); KEEP writes it into the
// pattern (one undo step).
class PatternVaryOverlay final : public Overlay, private juce::Timer
{
public:
    explicit PatternVaryOverlay (BatidaProcessor& p);
    ~PatternVaryOverlay() override;
    void openFor (int pattern);
    void startVary();
    std::function<void()> onSave;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh();

    BatidaProcessor& proc;
    int pattern = 0, shownVersion = -1;
    bool holding = false;
    Segmented amount { { "Subtle", "Medium", "Far" }, 1 };
    Segmented direction { { "Any", "Denser", "Sparser", "Broken", "Ghosts", "Rolls" } };
    juce::OwnedArray<juce::ToggleButton> trackLocks;
    SolidButton again { "VARY AGAIN", theme::lime };
    SolidButton keep { "KEEP", theme::ink };
    TextLink back { "BACK", true }, save { juce::String::fromUTF8 ("SAVE PATTERN\xe2\x80\xa6"), true };
    juce::OwnedArray<PatternTile> tiles; // original + 4
    juce::String status;
};
