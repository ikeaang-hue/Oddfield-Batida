#pragma once

#include "Look/Theme.h"

// Small building blocks for choices that aren't host parameters (lanes,
// library tabs, zoom, Vary directions...), in the same look as ParamControl.

// Segmented buttons: the white block slides to the chosen option (90 ms).
class Segmented final : public juce::Component
{
public:
    explicit Segmented (juce::StringArray options = {}, int selected = 0);

    void setOptions (juce::StringArray options);
    void setSelected (int index, bool notify = false);
    int getSelected() const { return selected; }
    std::function<void (int)> onChange;
    int getIdealWidth() const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> optionBounds (int index) const;
    juce::StringArray options;
    int selected = 0, from = 0;
    double slideStart = 0.0;
    std::unique_ptr<juce::VBlankAttachment> vblank;
};

// A borderless text button (UNDO, REDO, CFG): muted, white on hover, faint
// when disabled; optionally boxed.
class TextLink final : public juce::Button
{
public:
    explicit TextLink (const juce::String& text, bool boxed = false);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    bool boxed;
};

// A filled button: white with black text (KEEP, SAVE), or lime (VARY, PLAY).
class SolidButton final : public juce::Button
{
public:
    SolidButton (const juce::String& text, juce::Colour fill);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    juce::Colour fill;
    bool withDice = false;
};

// A page section: 1 px frame, a numbered tag top-left (e.g. "01 DYNAMICS").
// The area for its content is below the tag.
void drawPanel (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& tag);
juce::Rectangle<int> panelContent (juce::Rectangle<int> bounds);

// Lays controls out in rows of `rowHeight`, top to bottom.
struct RowLayout
{
    juce::Rectangle<int> area;
    int gap = 2;
    juce::Rectangle<int> next (int height = theme::kRowHeight)
    {
        auto r = area.removeFromTop (height);
        area.removeFromTop (gap);
        return r;
    }
};
