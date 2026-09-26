#pragma once

#include "Engine/Parameters.h"
#include "Engine/SampleData.h"

#include <array>
#include <memory>
#include <vector>

namespace batida
{

// Guided variations of one voice: nudges its settings randomly within limits
// that suit each parameter, steered by a direction, then renders and measures
// every candidate and drops the duds (silent, clipping, near-duplicates, or
// ones that moved the wrong way). Every candidate is level-matched to the
// original (through the voice's Level), so suggestions differ in tone, not
// volume. No learning, no network: fast and offline.

enum class VaryDirection { None, Brighter, Darker, Shorter, Longer, Tonal, Noisy, Cleaner, Dirtier };
enum class VaryGroup { None, Source, Fx, Envelopes };

VaryGroup varyGroupOf (int param); // None = never varied (level, pan, mode switches…)

struct VaryRequest
{
    VoiceParams base;
    const SampleData* sample = nullptr; // for Sample/Layer voices
    std::shared_ptr<const SampleData> keepAlive; // holds `sample` while a background job renders it
    float amount = 0.3f;                // 0 subtle .. 1 far
    VaryDirection direction = VaryDirection::None;
    bool lockSource = false, lockFx = false, lockEnvelopes = false;
    uint32_t seed = 1;
    int count = 4;
};

// What a sound measures as, rendered alone (dry) from one hit, for up to 4 s
// (it stops once the sound has died away).
constexpr int kSpectrumBands = 12;
struct SoundFeatures
{
    float peakDb = -120.0f, rmsDb = -120.0f, lengthMs = 0.0f;
    float brightnessHz = 0.0f;  // spectral centroid of the hit
    float loudnessDb = -120.0f; // the loudest 50 ms: how loud a hit sounds, whatever its length
    std::array<float, kSpectrumBands> bandsDb {}; // each band's share of the energy (40 Hz–16 kHz, log-spaced), dB
    bool finite = true;
};

SoundFeatures measureVoice (const VoiceParams& params, const SampleData* sample, double sampleRate = 48000.0);

// How different two sounds are, in rough "just noticeable" units: loudness,
// length, brightness and the shape of the spectrum.
float soundDistance (const SoundFeatures& a, const SoundFeatures& b);

// Whether a setting can be heard on this sound: sample settings on an FM
// sound (and the other way round), Balance outside Layer, and the settings of
// a silent operator can't. Vary never spends a try on those.
bool isAudibleSetting (const VoiceParams& base, int param);

// One hit rendered dry and reduced to `bins` peak levels over `seconds`, for
// drawing a sound's waveform (the Vary tiles).
std::vector<float> renderPeaks (const VoiceParams& params, const SampleData* sample, int bins, double seconds = 0.6,
                                double sampleRate = 24000.0);

struct VaryCandidate
{
    VoiceParams params;     // Level already includes levelDb
    SoundFeatures features; // measured with that Level
    float levelDb = 0.0f;   // the Level change that matches the original's loudness
    std::string note;       // its two biggest changes, e.g. "amp decay −41% · op2 level +6%"
};

// Names the two biggest changes from `base` to `changed` (Level left out: it's the loudness match).
std::string describeChanges (const VoiceParams& base, const VoiceParams& changed);

std::vector<VaryCandidate> vary (const VaryRequest& request);

} // namespace batida
