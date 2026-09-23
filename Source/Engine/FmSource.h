#pragma once

#include "Envelope.h"
#include "Parameters.h"

#include <array>
#include <cstdint>

namespace batida
{

// A fixed operator routing. Bit j of modulators[k] means op j modulates op k.
// Modulators always have a higher index than the op they modulate, so ops are
// computed from 4 down to 1. Op 4 carries the feedback in every algorithm.
struct Algorithm
{
    std::array<uint8_t, kNumOps> modulators;
    uint8_t carriers;
    const char* description;

    bool isCarrier (int op) const { return (carriers >> op) & 1; }
};

const std::array<Algorithm, kNumAlgorithms>& algorithms();

// Operator settings after the macros are applied. The UI shows these as the
// "effective" markers on the operator knobs.
struct FmOpEffective
{
    bool fixed = false;
    float ratio = 1.0f, fixedHz = 1000.0f, level = 0.0f, wave = 0.0f, fold = 0.0f;
    float attack = 0.0f, decay = 600.0f, sustain = 1.0f, release = 1500.0f;
    bool carrier = false;
};

struct FmEffective
{
    int algorithm = 0;
    float feedback = 0.0f;
    std::array<FmOpEffective, kNumOps> ops;
};

// Macros are offsets on top of the operator settings, and only act on
// modulators, so the carriers keep the pitch and level:
//   Harmonic  -1..0 pulls ratios to whole numbers, 0..1 blends through curated
//             inharmonic ratio sets.
//   Brightness  scales modulator levels (×0.5..×2, so depth ×0.25..×4).
//   Bright Decay  scales modulator decay/release times (×4..×0.25).
//   Grit      offsets feedback.
FmEffective computeEffectiveFm (const VoiceParams& p);

class FmSource
{
public:
    void prepare (double sampleRate);
    void setParameters (const FmEffective& fm);
    void trigger (bool oneShot, bool resetPhase);
    void release();
    void reset();

    bool isActive() const;

    // One sample at the given base frequency (ratio 1). The voice runs this
    // at twice its sample rate and decimates (see HalfbandDecimator).
    float process (float baseHz);

private:
    double sampleRate = 48000.0;
    float invSampleRate = 1.0f / 48000.0f;
    FmEffective fm;
    std::array<float, kNumOps> depth {}; // modulation depth in cycles
    float feedbackDepth = 0.0f;
    std::array<Envelope, kNumOps> env;
    std::array<float, kNumOps> phase {};
    std::array<uint32_t, kNumOps> noiseState { 0x9e3779b9u, 0x85ebca6bu, 0xc2b2ae35u, 0x27d4eb2fu };
    float feedbackSample = 0.0f;
};

} // namespace batida
