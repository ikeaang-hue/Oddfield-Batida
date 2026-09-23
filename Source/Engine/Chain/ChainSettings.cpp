#include "ChainSettings.h"

#include <algorithm>
#include <cmath>

namespace batida
{

ChainSettings computeEffectiveChain (const std::array<float, kNumGlobalParams>& g)
{
    ChainSettings s;
    const auto x = std::clamp (g[gp::XyX], 0.0f, 1.0f);
    const auto heat = std::clamp (g[gp::XyY], 0.0f, 1.0f);

    s.compAmount = g[gp::CompAmount];
    s.compAttack = g[gp::CompAttack];
    s.compRelease = g[gp::CompRelease];
    s.compMix = g[gp::CompMix];
    s.sidechain = (Detector) (int) (g[gp::CompDetector] + 0.5f) == Detector::Sidechain;

    s.drive = g[gp::DistDrive];
    s.type = g[gp::DistType];
    s.lowKeepHz = g[gp::DistLowKeep];
    s.excAmount = g[gp::ExcAmount];
    s.excToneHz = g[gp::ExcTone];

    s.hpHz = g[gp::EqHp];
    s.lpHz = g[gp::EqLp];
    s.lowDb = g[gp::EqLow];
    s.midDb = g[gp::EqMid];
    s.highDb = g[gp::EqHigh];

    if (g[gp::CompFollow] > 0.5f)
    {
        s.compAmount = std::clamp (s.compAmount + 0.6f * heat, 0.0f, 1.0f);
        s.compAttack = std::clamp (s.compAttack * std::exp2 (-1.7f * heat), 0.1f, 100.0f); // heat catches transients
    }

    if (g[gp::DistFollow] > 0.5f)
    {
        s.type = std::clamp (s.type + (x - 0.5f), 0.0f, 1.0f);
        s.drive = std::clamp (s.drive + heat, 0.0f, 1.0f);
        s.lowKeepHz = std::clamp (s.lowKeepHz * std::exp2 (1.5f * heat), 40.0f, 500.0f);
        s.excAmount = std::clamp (s.excAmount + 0.5f * heat, 0.0f, 1.0f);
        s.excToneHz = std::clamp (s.excToneHz * std::exp2 (x - 0.5f), 1000.0f, 16000.0f);
    }

    if (g[gp::EqFollow] > 0.5f)
    {
        // Tame the top with heat, only towards the aggressive/digital side.
        const auto harsh = std::clamp ((x - 0.3f) / 0.7f, 0.0f, 1.0f);
        s.highDb = std::clamp (s.highDb - 5.0f * heat * harsh, -12.0f, 12.0f);
    }

    s.outGain = std::pow (10.0f, g[gp::ChainOut] / 20.0f);
    s.safetyClip = g[gp::SafetyClip] > 0.5f;
    return s;
}

} // namespace batida
