#include "Compressor.h"

#include <algorithm>
#include <cmath>

namespace batida
{

namespace
{
constexpr float kKneeDb = 6.0f;

float coef (float ms, double sampleRate)
{
    return (float) std::exp (-1.0 / std::max (1.0, (double) ms * 0.001 * sampleRate));
}
} // namespace

void Compressor::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    makeupCoef = coef (1500.0f, sampleRate);
    reset();
}

void Compressor::reset()
{
    envelope = 0.0f;
    reductionDb = 0.0f;
    meanIn = meanOut = 0.0f;
}

void Compressor::setTimes (float attackMs, float releaseMs)
{
    attackCoef = coef (attackMs, sampleRate);
    releaseCoef = coef (releaseMs, sampleRate);
}

float Compressor::staticGainDb (float inputDb, float amount)
{
    if (amount <= 0.0f)
        return 0.0f;

    const auto threshold = -36.0f * amount;
    const auto ratio = 1.0f + 9.0f * amount;
    const auto over = inputDb - threshold;
    const auto slope = 1.0f / ratio - 1.0f;

    float gain;
    if (over <= -kKneeDb * 0.5f)
        gain = 0.0f;
    else if (over >= kKneeDb * 0.5f)
        gain = slope * over;
    else
    {
        const auto x = over + kKneeDb * 0.5f;
        gain = slope * x * x / (2.0f * kKneeDb);
    }

    return gain;
}

float Compressor::gainFor (float detectorLevel, float amount)
{
    // Peak envelope with separate attack and release.
    envelope = detectorLevel + (envelope - detectorLevel) * (detectorLevel > envelope ? attackCoef : releaseCoef);
    if (! std::isfinite (envelope)) // one bad input sample must not stick for good
        envelope = 0.0f;

    const auto levelDb = 20.0f * std::log10 (std::max (envelope, 1.0e-6f));
    reductionDb = amount > 0.0f ? staticGainDb (levelDb, amount) : 0.0f;

    return std::pow (10.0f, reductionDb / 20.0f);
}

float Compressor::makeupFor (float inputEnergy, float compressedEnergy)
{
    meanIn = inputEnergy + (meanIn - inputEnergy) * makeupCoef;
    meanOut = compressedEnergy + (meanOut - compressedEnergy) * makeupCoef;
    if (! std::isfinite (meanIn) || ! std::isfinite (meanOut))
        meanIn = meanOut = 0.0f;
    if (meanOut < 1.0e-9f)
        return 1.0f;
    return std::clamp (std::sqrt (meanIn / meanOut), 1.0f, 16.0f); // up to +24 dB
}

} // namespace batida
