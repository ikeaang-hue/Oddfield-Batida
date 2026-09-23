#include "FmSource.h"

#include <algorithm>
#include <cmath>

namespace batida
{

namespace
{
constexpr float kTwoPi = 6.283185307f;
constexpr float kHalfPi = 1.570796327f;

// Full modulation depth: level 1 = 2 cycles (4π radians). Depth follows
// level squared, so the knob feels even.
constexpr float kMaxDepthCycles = 2.0f;

// Full feedback, in cycles. Above roughly two thirds the operator turns
// chaotic, which is the noise source for snares and hats.
constexpr float kMaxFeedbackCycles = 0.6f;

constexpr uint8_t bit (int op) { return (uint8_t) (1u << op); }

// Inharmonic ratio multipliers per op, from mild detune to clangorous.
constexpr std::array<std::array<float, kNumOps>, 5> kInharmonicSets { {
    { 1.0f, 1.0f, 1.0f, 1.0f },
    { 1.0f, 1.0595f, 0.9439f, 1.1225f },
    { 1.0f, 1.4142f, 0.7071f, 1.7321f },
    { 1.0f, 1.5874f, 1.2599f, 2.3784f },
    { 1.0f, 1.3591f, 1.5708f, 2.2361f },
} };

float applyHarmonic (float ratio, float harm, int op)
{
    if (harm < 0.0f)
    {
        const auto target = ratio < 0.75f ? 0.5f : std::round (ratio);
        return ratio + (target - ratio) * -harm;
    }

    const auto t = harm * (float) (kInharmonicSets.size() - 1);
    const auto i = std::min ((int) t, (int) kInharmonicSets.size() - 2);
    const auto f = t - (float) i;
    const auto a = std::log (kInharmonicSets[(size_t) i][(size_t) op]);
    const auto b = std::log (kInharmonicSets[(size_t) i + 1][(size_t) op]);
    return ratio * std::exp (a + (b - a) * f);
}

inline float basicWave (int shape, float p)
{
    switch (shape)
    {
        case 0:  return std::sin (kTwoPi * p);
        case 1:  return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
        case 2:  return p < 0.5f ? 2.0f * p : 2.0f * p - 2.0f;
        default: return p < 0.5f ? 1.0f : -1.0f;
    }
}

// Sine → triangle → saw → square across 0..1, then an optional sine fold.
inline float shapeWave (float p, float morph, float fold)
{
    const auto m = morph * 3.0f;
    const auto i = std::min ((int) m, 2);
    const auto t = m - (float) i;

    auto w = basicWave (i, p);
    if (t > 0.0f)
        w += (basicWave (i + 1, p) - w) * t;

    if (fold > 0.0f)
    {
        const auto folded = std::sin (kHalfPi * w * (1.0f + 7.0f * fold));
        w += (folded - w) * std::min (1.0f, 4.0f * fold);
    }
    return w;
}
} // namespace

const std::array<Algorithm, kNumAlgorithms>& algorithms()
{
    static const std::array<Algorithm, kNumAlgorithms> table { {
        { { bit (1), bit (2), bit (3), 0 },                 bit (0),                           "4 > 3 > 2 > 1" },
        { { bit (1), (uint8_t) (bit (2) | bit (3)), 0, 0 }, bit (0),                           "(3 + 4) > 2 > 1" },
        { { (uint8_t) (bit (1) | bit (2)), 0, bit (3), 0 }, bit (0),                           "(4 > 3) + 2 > 1" },
        { { bit (1), 0, bit (3), 0 },                       (uint8_t) (bit (0) | bit (2)),     "4 > 3, 2 > 1" },
        { { bit (3), bit (3), bit (3), 0 },                 (uint8_t) (bit (0) | bit (1) | bit (2)), "4 > (1, 2, 3)" },
        { { 0, 0, bit (3), 0 },                             (uint8_t) (bit (0) | bit (1) | bit (2)), "4 > 3, 1, 2" },
        { { 0, bit (2), bit (3), 0 },                       (uint8_t) (bit (0) | bit (1)),     "4 > 3 > 2, 1" },
        { { 0, 0, 0, 0 },                                   (uint8_t) 0x0f,                    "1, 2, 3, 4" },
    } };
    return table;
}

FmEffective computeEffectiveFm (const VoiceParams& p)
{
    FmEffective fm;
    fm.algorithm = std::clamp (p.choice (vp::FmAlgo), 0, kNumAlgorithms - 1);

    const auto& alg = algorithms()[(size_t) fm.algorithm];
    const auto harm = std::clamp (p[vp::FmHarm], -1.0f, 1.0f);
    const auto levelScale = std::exp2 (std::clamp (p[vp::FmBright], -1.0f, 1.0f));
    const auto timeScale = std::exp2 (-2.0f * std::clamp (p[vp::FmBrightDecay], -1.0f, 1.0f));

    fm.feedback = std::clamp (p[vp::FmFeedback] + p[vp::FmGrit], 0.0f, 1.0f);

    for (int k = 0; k < kNumOps; ++k)
    {
        auto& op = fm.ops[(size_t) k];
        op.carrier = alg.isCarrier (k);
        op.fixed = p.op (k, Fixed) > 0.5f;
        op.ratio = p.op (k, Ratio);
        op.fixedHz = p.op (k, Freq);
        op.level = p.op (k, OpLevel);
        op.wave = p.op (k, Wave);
        op.fold = p.op (k, Fold);
        op.attack = p.op (k, OpA);
        op.decay = p.op (k, OpD);
        op.sustain = p.op (k, OpS);
        op.release = p.op (k, OpR);

        if (! op.carrier)
        {
            if (! op.fixed)
                op.ratio = std::clamp (applyHarmonic (op.ratio, harm, k), 0.125f, 32.0f);

            op.level = std::clamp (op.level * levelScale, 0.0f, 2.0f);
            op.decay = std::clamp (op.decay * timeScale, 5.0f, 8000.0f);
            op.release = std::clamp (op.release * timeScale, 5.0f, 8000.0f);
        }
    }

    return fm;
}

void FmSource::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    invSampleRate = (float) (1.0 / newSampleRate);
    for (auto& e : env)
        e.setSampleRate (newSampleRate);
    reset();
}

void FmSource::setParameters (const FmEffective& newFm)
{
    fm = newFm;

    for (int k = 0; k < kNumOps; ++k)
    {
        const auto& op = fm.ops[(size_t) k];
        env[(size_t) k].setParameters (op.attack, op.decay, op.sustain, op.release);
        depth[(size_t) k] = op.level * op.level * kMaxDepthCycles;
    }

    feedbackDepth = fm.feedback * kMaxFeedbackCycles;
}

void FmSource::trigger (bool oneShot, bool resetPhase)
{
    if (resetPhase)
    {
        phase.fill (0.0f);
        feedbackSample = 0.0f;
        for (auto& e : env)
            e.reset();
    }

    for (auto& e : env)
        e.trigger (oneShot);
}

void FmSource::release()
{
    for (auto& e : env)
        e.release();
}

void FmSource::reset()
{
    phase.fill (0.0f);
    feedbackSample = 0.0f;
    for (auto& e : env)
        e.reset();
}

bool FmSource::isActive() const
{
    const auto& alg = algorithms()[(size_t) fm.algorithm];
    for (int k = 0; k < kNumOps; ++k)
        if (alg.isCarrier (k) && env[(size_t) k].isActive())
            return true;
    return false;
}

float FmSource::process (float baseHz)
{
    const auto& alg = algorithms()[(size_t) fm.algorithm];
    std::array<float, kNumOps> modOut {};
    float out = 0.0f;

    for (int k = kNumOps - 1; k >= 0; --k)
    {
        const auto& op = fm.ops[(size_t) k];

        float mod = 0.0f;
        for (int j = k + 1; j < kNumOps; ++j)
            if ((alg.modulators[(size_t) k] >> j) & 1)
                mod += modOut[(size_t) j];

        if (k == kNumOps - 1)
            mod += feedbackDepth * feedbackSample;

        const auto hz = op.fixed ? op.fixedHz : baseHz * op.ratio;
        auto& ph = phase[(size_t) k];
        ph += hz * invSampleRate;
        ph -= std::floor (ph);

        auto p = ph + mod;
        p -= std::floor (p);

        const auto a = shapeWave (p, op.wave, op.fold) * env[(size_t) k].next();

        if (k == kNumOps - 1)
            feedbackSample = a;

        if (op.carrier)
            out += a * op.level;
        else
            modOut[(size_t) k] = a * depth[(size_t) k];
    }

    return out;
}

} // namespace batida
