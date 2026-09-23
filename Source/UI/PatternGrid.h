#pragma once

#include "Plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The step grid for the pattern shown (the one playing, or the Pattern
// parameter): 8 tracks and the XY lock row, 32 steps per page.
//
//   Gate lane:  click toggles, drag paints.
//   Value lanes (velocity, pitch, slice, probability): click or drag sets the
//               value from the height inside the cell.
//   Ratchet:    click cycles 1-4.
//   Option-click a cell: that track's length ends at this step.
//   XY row:     click locks the step to the pad's current position;
//               right-click clears it.
//   Right-click a track name: copy, paste, clear, fill.
class PatternGrid final : public juce::Component, private juce::Timer
{
public:
    enum class Lane { Gate, Velocity, Pitch, Slice, Ratchet, Probability };

    explicit PatternGrid (BatidaProcessor& p);
    ~PatternGrid() override;

    void setLane (Lane l);
    void setPage (int p);
    int getPage() const { return page; }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;

    static constexpr int kStepsPerPage = 32;
    static constexpr int kHeaderWidth = 170;
    static constexpr int kRowHeight = 26;
    static int preferredHeight() { return (batida::kNumTracks + 1) * kRowHeight + 2; }

private:
    void timerCallback() override;

    juce::Rectangle<int> cellBounds (int row, int step) const; // step within the page
    bool hitCell (juce::Point<int> p, int& row, int& step) const;
    void editCell (int row, int step, juce::Point<int> p, bool first);
    void trackMenu (int track);

    BatidaProcessor& proc;
    Lane lane = Lane::Gate;
    int page = 0;
    bool paintGate = true; // value painted by a gate-lane drag
    int lastEditedRow = -1, lastEditedStep = -1;

    std::array<juce::Slider, batida::kNumTracks> chainAmount;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, batida::kNumTracks> chainAttachments;

    int shownVersion = -1, shownPattern = -1;
    std::array<int, batida::kNumTracks + 1> shownSteps {};
};
