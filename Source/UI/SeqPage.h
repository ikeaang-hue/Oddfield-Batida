#pragma once

#include "BrowseStrip.h"
#include "DraggableNumber.h"
#include "Frame.h"
#include "PatternGrid.h"
#include "Widgets.h"

// The 16 pattern slots: filled = has steps, inverted = shown, lime = playing.
// Click one to show it (the Pattern parameter).
class PatternSlots final : public juce::Component, private juce::Timer
{
public:
    explicit PatternSlots (BatidaProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }
    juce::Rectangle<int> slot (int i) const;
    BatidaProcessor& proc;
};

// SEQ: pattern strip and slots, tempo, sync, play; length, run, start on,
// latch, swing; the lane, the bar and copy/paste/clear; then the tracks (sound
// cells with every gesture of the sound row) beside the grid, and the XY lane.
class SeqPage final : public juce::Component, private juce::Timer
{
public:
    explicit SeqPage (BatidaProcessor& p);
    ~SeqPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    BrowseStrip patternStrip { BrowseStrip::Style::Large }; // the editor wires it to the library
    std::array<SoundCell*, batida::kNumTracks> getTrackCells();
    PatternGrid& getGrid() { return grid; }

private:
    void timerCallback() override;
    void setLength (int length);

    BatidaProcessor& proc;
    PatternGrid grid;
    PageMap pageMap;
    PatternSlots slots;
    juce::OwnedArray<SoundCell> tracks;

    DraggableNumber tempo;
    ParamControl sync, play, run, quantise, latch, swing;
    TextLink shorter { juce::String::fromUTF8 ("\xe2\x88\x92"), true }, longer { "+", true };
    TextLink copyButton { "COPY", true }, pasteButton { "PASTE", true }, clearButton { "CLEAR", true };
    Segmented lanes { { "Steps", "Pitch", "Slice", "Ratchet", "Prob" } };
    juce::Rectangle<int> lengthBox, lengthLabel, rulerBounds, xyHeaderBounds, pageText;
    int shownLength = -1, shownStep = -2, shownPage = -1;
};
