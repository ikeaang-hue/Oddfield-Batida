#include "ChainSettings.h"

#include <algorithm>
#include <cmath>

namespace batida
{

namespace
{
constexpr float kHeatComp = 0.25f;    // compressor amount added at full heat
constexpr float kHeatAttack = 0.6f;   // attack ×2^0.6 ≈ ×1.5 at full heat
constexpr float kHeatLowKeep = 0.5f;  // crossover ×2^0.5 ≈ ×1.4 at full heat
constexpr float kHeatLiftDb = 3.0f;   // output lift at full heat (rising early, like the distortion's loss)
} // namespace

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

    // Heat destroys by crushing, not by squashing: it adds a little
    // compression, with a slightly longer attack so the kicks' transients
    // punch through, and puts the weight on the distortion.
    if (g[gp::CompFollow] > 0.5f)
    {
        s.compAmount = std::clamp (s.compAmount + kHeatComp * heat, 0.0f, 1.0f);
        s.compAttack = std::clamp (s.compAttack * std::exp2 (kHeatAttack * heat), 0.1f, 100.0f);
    }

    if (g[gp::DistFollow] > 0.5f)
    {
        s.type = std::clamp (s.type + (x - 0.5f), 0.0f, 1.0f);
        s.drive = std::clamp (s.drive + heat, 0.0f, 1.0f);
        s.lowKeepHz = std::clamp (s.lowKeepHz * std::exp2 (kHeatLowKeep * heat), 40.0f, 500.0f); // the sub stays; a kick's body crushes
        s.excAmount = std::clamp (s.excAmount + 0.5f * heat, 0.0f, 1.0f);
        s.excToneHz = std::clamp (s.excToneHz * std::exp2 (x - 0.5f), 1000.0f, 16000.0f);
    }

    if (g[gp::EqFollow] > 0.5f)
    {
        // Tame the top with heat, only towards the aggressive/digital side.
        const auto harsh = std::clamp ((x - 0.3f) / 0.7f, 0.0f, 1.0f);
        s.highDb = std::clamp (s.highDb - 5.0f * heat * harsh, -12.0f, 12.0f);
    }

    // Destroyed sounds crushed, not muted: clipping a kick's peaks costs it
    // loudness, so heat lifts the output a little (measured on the factory
    // kits: full heat ends about level with clean, the kick alone too).
    s.outGain = std::pow (10.0f, (g[gp::ChainOut] + (g[gp::DistFollow] > 0.5f ? kHeatLiftDb * std::sqrt (heat) : 0.0f)) / 20.0f);
    s.safetyClip = g[gp::SafetyClip] > 0.5f;
    return s;
}

} // namespace batida
