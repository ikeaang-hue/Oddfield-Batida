#pragma once

#include "Engine/Sequencer/Pattern.h"

#include <juce_core/juce_core.h>

#include <deque>
#include <optional>

// Undo/redo for what the DAW can't undo: pattern edits, names, and big
// one-click actions (Vary Keep, scene recall, slot swaps). Single knob moves
// are left to the host's own undo, so the two histories never overlap.
//
// A step is taken *before* a change (beginStep) and only kept if something
// actually changed by the time the next step starts, so a click that changes
// nothing doesn't leave an empty step.
struct HistorySnapshot
{
    batida::PatternBank patterns;
    std::array<juce::String, batida::kNumVoices> names;
    std::optional<std::vector<float>> parameters;                        // normalised, all parameters
    std::optional<std::array<juce::String, batida::kNumVoices>> samples; // paths
};

class History
{
public:
    static constexpr size_t kMaxSteps = 100;

    // stamp: anything that changes whenever the undoable state changes.
    void beginStep (HistorySnapshot before, juce::int64 stamp)
    {
        finalise (stamp);
        pending = std::move (before);
        pendingStamp = stamp;
    }

    void finalise (juce::int64 stamp)
    {
        if (pending && stamp != pendingStamp)
        {
            undoStack.push_back (std::move (*pending));
            if (undoStack.size() > kMaxSteps)
                undoStack.pop_front();
            redoStack.clear();
        }
        pending.reset();
    }

    bool canUndo (juce::int64 stamp) const { return ! undoStack.empty() || (pending && stamp != pendingStamp); }
    bool canRedo() const { return ! redoStack.empty(); }

    // Returns the snapshot to restore; `now` is the current state (kept for redo/undo).
    std::optional<HistorySnapshot> undo (HistorySnapshot now, juce::int64 stamp)
    {
        finalise (stamp);
        if (undoStack.empty())
            return std::nullopt;
        auto s = std::move (undoStack.back());
        undoStack.pop_back();
        redoStack.push_back (std::move (now));
        return s;
    }

    std::optional<HistorySnapshot> redo (HistorySnapshot now)
    {
        pending.reset();
        if (redoStack.empty())
            return std::nullopt;
        auto s = std::move (redoStack.back());
        redoStack.pop_back();
        undoStack.push_back (std::move (now));
        return s;
    }

private:
    std::deque<HistorySnapshot> undoStack, redoStack;
    std::optional<HistorySnapshot> pending;
    juce::int64 pendingStamp = 0;
};
