#pragma once

#include "Engine/Parameters.h"

namespace batida
{

// The kit chain's settings after the XY pad is applied. Like the FM macros,
// the pad is an offset over the stage knobs, and each stage can opt out with
// its Follow XY toggle. The UI shows these as the effective-value markers.
//
//   X (character): moves Type around its knob (knob default = middle, so the
//                  pad reads warm → digital); the exciter tone follows.
//   Y (heat):      adds drive, compressor amount and exciter amount, shortens
//                  the compressor attack, and raises the low-keep crossover;
//                  the EQ tames the top towards the aggressive/digital side.
struct ChainSettings
{
    float compAmount = 0.0f, compAttack = 10.0f, compRelease = 120.0f, compMix = 1.0f;
    bool sidechain = false;

    float drive = 0.0f, type = 0.5f, lowKeepHz = 90.0f, excAmount = 0.0f, excToneHz = 5000.0f;

    float hpHz = 20.0f, lpHz = 20000.0f, lowDb = 0.0f, midDb = 0.0f, highDb = 0.0f;

    float outGain = 1.0f;
    bool safetyClip = true;
};

ChainSettings computeEffectiveChain (const std::array<float, kNumGlobalParams>& g);

} // namespace batida
