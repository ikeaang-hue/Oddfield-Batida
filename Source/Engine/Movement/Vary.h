#pragma once

#include "Engine/Parameters.h"
#include "Engine/SampleData.h"

#include <vector>

namespace batida
{

// Guided variations of one voice: nudges its settings randomly within limits
// that suit each parameter, steered by a direction, then renders and measures
// every candidate and drops the duds (silent, clipping, near-duplicates, or
// ones that moved the wrong way). No learning, no network: fast and offline.

enum class VaryDirection { None, Brighter, Darker, Shorter, Longer, Tonal, Noisy, Cleaner, Dirtier };
enum class VaryGroup { None, Source, Fx, Envelopes };

VaryGroup varyGroupOf (int param); // None = never varied (level, pan, mode switches…)

struct VaryRequest
{
    VoiceParams base;
    const SampleData* sample = nullptr; // for Sample/Layer voices
    float amount = 0.3f;                // 0 subtle .. 1 far
    VaryDirection direction = VaryDirection::None;
    bool lockSource = false, lockFx = false, lockEnvelopes = false;
    uint32_t seed = 1;
    int count = 4;
};

// What a sound measures as, rendered alone (dry) from one hit.
struct SoundFeatures
{
    float peakDb = -120.0f, rmsDb = -120.0f, lengthMs = 0.0f, brightnessHz = 0.0f;
    bool finite = true;
};

SoundFeatures measureVoice (const VoiceParams& params, const SampleData* sample, double sampleRate = 48000.0);

struct VaryCandidate
{
    VoiceParams params;
    SoundFeatures features;
};

std::vector<VaryCandidate> vary (const VaryRequest& request);

} // namespace batida
