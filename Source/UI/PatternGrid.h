#pragma once

#include "Plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The step grid for the pattern shown (the one playing, or the Pattern
// parameter): 8 tracks and the XY lock row, one bar (16 steps) per page.
//
//   Steps lane:  tap toggles a step; press and drag up/down sets its velocity
//                from where it was; dragging to the bottom turns it off.
//                Shift-drag paints steps on across the row.
//   Other lanes: drag up/down changes the value from where it was
//                (Ratchet: tap cycles 1-4).
//   XY row:      tap locks the step to the pad's position (tap again clears);
//                drag moves the lock: up/down = heat, left/right = character.
//   Option-click a step: that track's length ends there (polymeter).
//   Track names: double-click to rename; right-click for rename, swap,
//   copy, paste, clear, fill. M / S under each name mute and solo the voice.
class PatternGrid final : public juce::Component, private juce::Timer
{
public:
    enum class Lane { Steps, Pitch, Slice, Ratchet, Probability };

    explicit PatternGrid (BatidaProcessor& p);
    ~PatternGrid() override;

    void setLane (Lane l);
    void setPage (int p);
    int getPage() const { return page; }
    std::function<void()> onPageChanged;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void resized() override;

    static constexpr int kStepsPerPage = 16;
    static constexpr int kPages = batida::kMaxSteps / kStepsPerPage;
    static constexpr int kHeaderWidth = 130;

private:
    void timerCallback() override;

    int rowHeight() const { return getHeight() / (batida::kNumTracks + 1); }
    juce::Rectangle<int> cellBounds (int row, int step) const; // step within the page
    bool hitCell (juce::Point<int> p, int& row, int& step) const;
    int patternIndex() const;
    void trackMenu (int track);

    BatidaProcessor& proc;
    Lane lane = Lane::Steps;
    int page = 0;

    // The press being handled
    int pressRow = -1, pressStep = -1;   // absolute step
    bool moved = false, painting = false;
    batida::Step startStep;
    batida::XyLock startLock;
    int lastPaintStep = -1;

    std::array<juce::TextButton, batida::kNumTracks> muteButtons, soloButtons;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, batida::kNumTracks> muteAttachments,
        soloAttachments;
    std::unique_ptr<juce::TextEditor> renameEditor;
    void renameTrack (int track);

    int shownVersion = -1, shownPattern = -1, shownNames = -1;
    std::array<int, batida::kNumTracks + 1> shownSteps {};
};

// Overview of all 64 steps: every track's hits as dots, the bar on screen
// outlined, the playhead, and steps beyond the pattern dimmed. Click a bar to
// show it.
class PageMap final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    PageMap (BatidaProcessor& p, PatternGrid& grid);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    void timerCallback() override { repaint(); }

    BatidaProcessor& proc;
    PatternGrid& grid;
};
