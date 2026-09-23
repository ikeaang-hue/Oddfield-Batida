#include "Envelope.h"

#include <algorithm>
#include <cmath>

namespace batida
{

float Envelope::coefficientFor (float ms, double sampleRate)
{
    // Falls by 60 dB (a factor of 1000) over the given time.
    const auto samples = std::max (1.0, (double) ms * 0.001 * sampleRate);
    return (float) std::exp (-std::log (1000.0) / samples);
}

void Envelope::setParameters (float attackMs, float decayMs, float sustainLevel, float releaseMs)
{
    const auto attackSamples = (double) attackMs * 0.001 * sampleRate;
    attackStep = attackSamples < 1.0 ? 1.0f : (float) (1.0 / attackSamples);
    decayCoef = coefficientFor (decayMs, sampleRate);
    releaseCoef = coefficientFor (releaseMs, sampleRate);
    sustain = std::clamp (sustainLevel, 0.0f, 1.0f);
}

void Envelope::trigger (bool oneShot)
{
    oneShotMode = oneShot;
    stage = Stage::Attack;
}

void Envelope::release()
{
    if (stage != Stage::Idle && ! oneShotMode)
        stage = Stage::Release;
}

void Envelope::reset()
{
    stage = Stage::Idle;
    value = 0.0f;
}

} // namespace batida
