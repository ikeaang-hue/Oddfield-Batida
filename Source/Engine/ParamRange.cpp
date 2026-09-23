#include "ParamRange.h"

namespace batida
{

juce::NormalisableRange<float> rangeFor (const ParamSpec& spec)
{
    if (spec.kind != Kind::Float)
        return { spec.min, spec.max, 1.0f }; // choices and toggles: index steps

    juce::NormalisableRange<float> range (spec.min, spec.max, spec.step);
    if (spec.centre > spec.min && spec.centre < spec.max && spec.centre != 0.5f * (spec.min + spec.max))
        range.setSkewForCentre (spec.centre);
    return range;
}

namespace
{
std::vector<juce::NormalisableRange<float>> build (const std::vector<ParamSpec>& specs)
{
    std::vector<juce::NormalisableRange<float>> r;
    for (const auto& s : specs)
        r.push_back (rangeFor (s));
    return r;
}
} // namespace

const std::vector<juce::NormalisableRange<float>>& voiceRanges()
{
    static const auto r = build (voiceParamSpecs());
    return r;
}

const std::vector<juce::NormalisableRange<float>>& globalRanges()
{
    static const auto r = build (globalParamSpecs());
    return r;
}

} // namespace batida
