#pragma once

#include "Parameters.h"

#include <juce_core/juce_core.h>

namespace batida
{

// The knob mapping of every parameter (range, step, skew), shared by the
// plugin's host parameters and the engine, so modulation offsets in 0..1
// move a value exactly as turning its knob would.
juce::NormalisableRange<float> rangeFor (const ParamSpec& spec);

const std::vector<juce::NormalisableRange<float>>& voiceRanges();  // by voice param index
const std::vector<juce::NormalisableRange<float>>& globalRanges(); // by global param index

// Only continuous parameters can be modulated or varied.
inline bool isContinuous (const ParamSpec& spec) { return spec.kind == Kind::Float; }

} // namespace batida
