#include "Saturator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace batida
{

namespace
{
constexpr int kDriveSteps = 33;
constexpr int kTypeSteps = 17;

// Drive 0..1 maps to 0..+36 dB into the shaper.
inline float driveGain (float drive) { return std::pow (10.0f, drive * 1.8f); }

struct GainTable
{
    std::array<std::array<float, kTypeSteps>, kDriveSteps> comp {};
};
} // namespace

void Saturator::reset()
{
    held.fill (0.0f);
    holdCounter.fill (0);
}

float Saturator::shape (int index, float x, float drive, int channel)
{
    const auto g = driveGain (drive);

    switch (index)
    {
        case 0: // tape: smooth, compressive
            return std::tanh (0.7f * g * x);

        case 1: // tube: asymmetric, adds even harmonics (DC removed after the stage)
            return std::tanh (g * x + 0.3f) - 0.291312612f; // tanh (0.3)

        case 2: // clip: hard
            return std::clamp (g * x, -1.0f, 1.0f);

        case 3: // fold: sine wavefolder
            return std::sin (1.570796327f * g * x);

        default: // crush: sample-and-hold decimation plus bit reduction
        {
            auto& counter = holdCounter[(size_t) channel];
            if (--counter < 0)
            {
                counter = (1 + (int) (drive * drive * 15.0f)) * kOversampling - 1;
                const auto steps = std::exp2 (16.0f - 12.0f * drive);
                const auto pre = std::clamp (x * (1.0f + 3.0f * drive), -1.0f, 1.0f);
                held[(size_t) channel] = std::round (pre * steps) / steps;
            }
            return held[(size_t) channel];
        }
    }
}

float Saturator::compensation (float drive, float type)
{
    static const GainTable table = []
    {
        // Measure each drive/type pair with noise at a drum-like level (-20 dB RMS).
        GainTable t;
        constexpr int n = 4096;
        std::array<float, n> noise {};
        uint32_t seed = 0x12345678u;
        double inSq = 0.0;
        for (auto& v : noise)
        {
            seed = seed * 1664525u + 1013904223u;
            v = ((float) (seed >> 8) / 8388608.0f - 1.0f) * 0.173f; // uniform, RMS ≈ 0.1
            inSq += (double) v * v;
        }

        for (int d = 0; d < kDriveSteps; ++d)
        {
            for (int k = 0; k < kTypeSteps; ++k)
            {
                Saturator s;
                const auto drive = (float) d / (kDriveSteps - 1);
                const auto m = (float) k / (kTypeSteps - 1) * (kNumShapes - 1);
                const auto i = std::min ((int) m, kNumShapes - 2);
                const auto f = m - (float) i;
                double outSq = 0.0;
                for (const auto x : noise)
                {
                    auto y = s.shape (i, x, drive, 0);
                    if (f > 0.0f)
                        y += (s.shape (i + 1, x, drive, 0) - y) * f;
                    outSq += (double) y * y;
                }
                t.comp[(size_t) d][(size_t) k] = (float) std::clamp (std::sqrt (inSq / std::max (outSq, 1.0e-12)), 0.05, 4.0);
            }
        }
        return t;
    }();

    const auto dp = std::clamp (drive, 0.0f, 1.0f) * (kDriveSteps - 1);
    const auto tp = std::clamp (type, 0.0f, 1.0f) * (kTypeSteps - 1);
    const auto d0 = std::min ((int) dp, kDriveSteps - 2);
    const auto t0 = std::min ((int) tp, kTypeSteps - 2);
    const auto fd = dp - (float) d0, ft = tp - (float) t0;
    const auto& c = table.comp;
    const auto top = c[(size_t) d0][(size_t) t0] + (c[(size_t) d0][(size_t) t0 + 1] - c[(size_t) d0][(size_t) t0]) * ft;
    const auto bottom = c[(size_t) d0 + 1][(size_t) t0] + (c[(size_t) d0 + 1][(size_t) t0 + 1] - c[(size_t) d0 + 1][(size_t) t0]) * ft;
    return top + (bottom - top) * fd;
}

float Saturator::process (float x, float drive, float type, int channel)
{
    if (drive <= 0.0f)
        return x;

    const auto m = std::clamp (type, 0.0f, 1.0f) * (kNumShapes - 1);
    const auto i = std::min ((int) m, kNumShapes - 2);
    const auto f = m - (float) i;

    auto y = shape (i, x, drive, channel);
    if (f > 0.0f)
        y += (shape (i + 1, x, drive, channel) - y) * f;

    // Fade the effect in over the first 10% of drive, so 0 is truly clean.
    const auto mix = std::min (1.0f, drive * 10.0f);
    return x + (y * compensation (drive, type) - x) * mix;
}

} // namespace batida
