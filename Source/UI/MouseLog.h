#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Review builds only: a log of the clicks the editor's process receives, and
// what JUCE does with them, for finding out why a host's clicks don't arrive
// (e.g. Logic running Batida out of process). Written to
// ~/Library/Logs/Oddfield/Batida mouse.log.
class MouseLog
{
public:
    virtual ~MouseLog() = default;
    static std::unique_ptr<MouseLog> start (juce::Component& editor); // nullptr in release builds
};
