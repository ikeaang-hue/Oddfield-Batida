#pragma once

#include "PresetFiles.h"

#include <vector>

// Checks a library file for everything loading lets slide (SPEC §10): loading
// is forgiving (missing settings take their defaults, values are clamped,
// unknown attributes are skipped), so a typo would otherwise go unnoticed.
// Used by `batida validate` and the tests.

namespace batida
{

struct Problem
{
    juce::String where; // the element and attribute, e.g. "KIT > VOICE 3 > flt_cuttoff"
    juce::String what;  // e.g. "unknown setting (did you mean flt_cutoff?)"
    bool fatal = false; // the file doesn't load at all
};

struct Validation
{
    std::optional<PresetType> type;
    std::vector<Problem> problems;

    bool loads() const;
    bool clean() const { return problems.empty(); }
};

Validation validateFile (const juce::File& file);

// The closest known key to a misspelt one (empty when nothing is close).
juce::String nearestKey (const juce::String& key, const juce::StringArray& known);

} // namespace batida
