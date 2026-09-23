#include "Modulators.h"

#include <cmath>

namespace batida
{

double syncRateQuarters (int i)
{
    static constexpr double lengths[] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75,
                                          2.0 / 3.0, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0, 32.0 };
    return lengths[std::clamp (i, 0, (int) std::size (lengths) - 1)];
}

void Modulators::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    reset();
}

void Modulators::reset()
{
    freePhase.fill (0.0);
    shotPhase.fill (0.0);
    shotRunning.fill (false);
    triggered.fill (false);
    values.fill (0.0f);
    smoothed.fill (0.0f);
    drift.fill (0.0f);
    lastPhase.fill (0.0);
    pendingPatternStart = false;
    pendingHits.fill (false);
}

void Modulators::hit (int voice)
{
    if (voice >= 0 && voice < kNumVoices)
        pendingHits[(size_t) voice] = true;
}

void Modulators::patternStart()
{
    pendingPatternStart = true;
}

void Modulators::advance (int numSamples, double ppq, double ppqPerSample, const std::array<float, kNumGlobalParams>& g,
                          const MovementData& data)
{
    bool anyHit = false;
    for (auto h : pendingHits)
        anyHit = anyHit || h;

    for (int m = 0; m < kNumMods; ++m)
    {
        const auto mode = (ModulatorMode) std::clamp ((int) (g[(size_t) modParam (m, ModMode)] + 0.5f), 0, 2);
        const auto quarters = syncRateQuarters ((int) (g[(size_t) modParam (m, ModRate)] + 0.5f));
        const auto trigger = (int) (g[(size_t) modParam (m, ModTrigger)] + 0.5f); // 0 any, 1 pattern, 2.. voice

        double phase = 0.0;
        switch (mode)
        {
            case ModulatorMode::Sync:
                phase = ppq / quarters;
                phase -= std::floor (phase);
                break;

            case ModulatorMode::Free:
                freePhase[(size_t) m] += g[(size_t) modParam (m, ModHz)] * numSamples / sampleRate;
                freePhase[(size_t) m] -= std::floor (freePhase[(size_t) m]);
                phase = freePhase[(size_t) m];
                break;

            case ModulatorMode::OneShot:
            {
                const bool fired = trigger == 0 ? anyHit
                                 : trigger == 1 ? pendingPatternStart
                                                : pendingHits[(size_t) std::clamp (trigger - 2, 0, kNumVoices - 1)];
                if (fired)
                {
                    shotPhase[(size_t) m] = 0.0;
                    shotRunning[(size_t) m] = true;
                }
                if (shotRunning[(size_t) m])
                {
                    shotPhase[(size_t) m] += numSamples * ppqPerSample / quarters;
                    // Finished: hold the shape's last point (not the wrap back to the first).
                    const auto& shape = data.mods[(size_t) m];
                    const auto end = shape.numPoints > 0 ? (double) shape.points[(size_t) shape.numPoints - 1].x : 1.0;
                    if (shotPhase[(size_t) m] >= end)
                    {
                        shotPhase[(size_t) m] = end;
                        shotRunning[(size_t) m] = false;
                    }
                }
                phase = shotPhase[(size_t) m];
                break;
            }
        }

        // Humanise: a new small random offset each cycle.
        if (phase < lastPhase[(size_t) m])
            drift[(size_t) m] = random.nextFloat() * 2.0f - 1.0f;
        lastPhase[(size_t) m] = phase;

        auto v = evaluateShape (data.mods[(size_t) m], (float) phase);
        if (g[(size_t) modParam (m, ModPolarity)] > 0.5f)
            v = 2.0f * v - 1.0f;
        v += g[(size_t) modParam (m, ModHuman)] * 0.25f * drift[(size_t) m];

        // Smooth: up to 0.5 s of slew.
        const auto s = g[(size_t) modParam (m, ModSmooth)];
        if (s > 0.0f)
        {
            const auto ms = 1.0 + 499.0 * s * s;
            const auto coef = (float) std::exp (-numSamples / (ms * 0.001 * sampleRate));
            smoothed[(size_t) m] = v + (smoothed[(size_t) m] - v) * coef;
            v = smoothed[(size_t) m];
        }
        else
            smoothed[(size_t) m] = v;

        values[(size_t) m] = v * g[(size_t) modParam (m, ModAmount)];
        uiValue[(size_t) m] = values[(size_t) m];
        uiPhase[(size_t) m] = (float) phase;
    }

    pendingPatternStart = false;
    pendingHits.fill (false);
}

} // namespace batida
