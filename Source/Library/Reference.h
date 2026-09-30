#pragma once

#include "PresetFiles.h"

// The reference of every file element and setting (SPEC §10), generated from
// the parameter specs so it can't drift from the code: docs/REFERENCE.md is
// this text (a test compares them), and `batida params` prints the same data.

namespace batida
{

juce::String referenceMarkdown();

// Every setting as data: [{ key, element, label, kind, min, max, default,
// unit, choices, description, hostId, ... }].
juce::var parameterData();

// What a setting does, in a line (every parameter has one; a test checks).
juce::String describeVoiceParam (int param);
juce::String describeGlobalParam (int param);

// The unit as a reader needs it: "ms", "Hz", "0..1" for the plain amounts...
juce::String unitLabel (const ParamSpec& spec);

// The smallest useful file of each level, as the reference shows them (the
// tests load and render each one).
juce::String exampleFile (PresetType type);

} // namespace batida
