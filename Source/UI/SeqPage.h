#pragma once

#include "BrowseStrip.h"
#include "DraggableNumber.h"
#include "Frame.h"
#include "PatternGrid.h"
#include "PatternVaryOverlay.h"
#include "Widgets.h"

// MIDI: drag it onto a track in any DAW to get the pattern as a MIDI region;
// click to save the file (User/MIDI) and show it in Finder.
class MidiHandle final : public juce::Component, public juce::SettableTooltipClient
{
public:
    MidiHandle();
    std::function<juce::File()> onExport;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool dragging = false;
};

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
    std::function<void()> onSavePattern; // Vary's Save pattern

    // A drum loop dragged over the grid becomes a kit and this pattern.
    void setBreakDrop (bool on);
    void closeOverlays() { vary.close(); }
    bool isOverlayOpen() const { return vary.isVisible(); }
    void paintOverChildren (juce::Graphics&) override;

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
    SolidButton varyButton { "VARY", theme::lime };
    MidiHandle midi;
    PatternVaryOverlay vary;
    bool breakDrop = false;
    Segmented lanes { { "Steps", "Pitch", "Slice", "Ratchet", "Prob" } };
    juce::Rectangle<int> lengthBox, lengthLabel, rulerBounds, xyHeaderBounds, pageText;
    int shownLength = -1, shownStep = -2, shownPage = -1;
};
