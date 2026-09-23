#include "Processors.h"

#include <algorithm>
#include <cmath>

namespace batida
{

namespace
{
float followerCoef (float ms, double sampleRate)
{
    return (float) std::exp (-1.0 / std::max (1.0, (double) ms * 0.001 * sampleRate));
}
} // namespace

void Smoother::setTime (float ms, double sampleRate)
{
    coef = followerCoef (ms, sampleRate);
}

// Punch ----------------------------------------------------------------------

void Punch::prepare (double sampleRate)
{
    fastAttack = followerCoef (0.3f, sampleRate);
    fastRelease = followerCoef (15.0f, sampleRate);
    slowAttack = followerCoef (15.0f, sampleRate);
    slowRelease = followerCoef (80.0f, sampleRate);
    reset();
}

void Punch::reset()
{
    fast = slow = 0.0f;
}

void Punch::process (float& left, float& right, float amount)
{
    const auto x = std::max (std::abs (left), std::abs (right));
    fast = x + (fast - x) * (x > fast ? fastAttack : fastRelease);
    slow = x + (slow - x) * (x > slow ? slowAttack : slowRelease);

    if (amount <= 0.0f)
        return;

    const auto ratio = (fast + 1.0e-5f) / (slow + 1.0e-5f);
    const auto gain = std::clamp (std::exp2 (amount * std::log2 (ratio)), 0.1f, 4.0f);
    left *= gain;
    right *= gain;
}

// Drive ----------------------------------------------------------------------

void Drive::reset()
{
    held.fill (0.0f);
    holdCounter = 0;
}

float Drive::shape (float x, float amount, DriveType type)
{
    switch (type)
    {
        case DriveType::Soft:
            return std::tanh ((1.0f + 24.0f * amount) * x) * (1.0f - 0.4f * amount);

        case DriveType::Hard:
            return std::clamp ((1.0f + 24.0f * amount) * x, -1.0f, 1.0f) * (1.0f - 0.4f * amount);

        case DriveType::Fold:
            return std::sin (1.570796327f * (1.0f + 8.0f * amount) * x) * (1.0f - 0.3f * amount);

        case DriveType::Crush:
            break; // handled in process(), both channels together
    }
    return x;
}

void Drive::process (float& left, float& right, float amount, DriveType type)
{
    if (amount <= 0.0f)
        return;

    float l, r;

    if (type == DriveType::Crush)
    {
        // Sample-and-hold for rate reduction, then quantise.
        if (--holdCounter < 0)
        {
            holdCounter = (int) (amount * amount * 40.0f);
            const auto steps = std::exp2 (16.0f - 13.0f * amount);
            held[0] = std::round (left * steps) / steps;
            held[1] = std::round (right * steps) / steps;
        }
        l = held[0];
        r = held[1];
    }
    else
    {
        l = shape (left, amount, type);
        r = shape (right, amount, type);
    }

    // Fade in the effect over the first 5% of the knob so there is no step.
    const auto mix = std::min (1.0f, amount * 20.0f);
    left += (l - left) * mix;
    right += (r - right) * mix;
}

// HalfbandDecimator ------------------------------------------------------------

namespace
{
double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 32; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

const std::array<float, HalfbandDecimator::kTaps>& halfbandTaps()
{
    static const auto taps = []
    {
        constexpr int n = HalfbandDecimator::kTaps;
        constexpr double beta = 8.0;
        std::array<float, n> h {};
        double sum = 0.0;
        std::array<double, n> d {};

        for (int i = 0; i < n; ++i)
        {
            const auto k = i - (n - 1) / 2;
            const auto sinc = k == 0 ? 0.5 : std::sin (3.141592653589793 * k / 2.0) / (3.141592653589793 * k);
            const auto r = 2.0 * i / (n - 1) - 1.0;
            d[(size_t) i] = sinc * besselI0 (beta * std::sqrt (1.0 - r * r)) / besselI0 (beta);
            sum += d[(size_t) i];
        }
        for (int i = 0; i < n; ++i)
            h[(size_t) i] = (float) (d[(size_t) i] / sum);
        return h;
    }();
    return taps;
}
} // namespace

void HalfbandDecimator::reset()
{
    history.fill (0.0f);
    pos = 0;
}

void HalfbandDecimator::push (float x)
{
    // Written twice so the last kTaps samples are always contiguous.
    history[(size_t) pos] = x;
    history[(size_t) (pos + kTaps)] = x;
    pos = (pos + 1) % kTaps;
}

float HalfbandDecimator::process (float first, float second)
{
    push (first);
    push (second);

    const auto& h = halfbandTaps();
    const auto* x = history.data() + pos;
    float y = 0.0f;
    for (int i = 0; i < kTaps; ++i)
        y += h[(size_t) i] * x[i];
    return y;
}

// Filter ---------------------------------------------------------------------

void SvfFilter::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    lastCutoff = -1.0f;
    reset();
}

void SvfFilter::reset()
{
    ic1.fill (0.0f);
    ic2.fill (0.0f);
}

void SvfFilter::update (float cutoffHz, float resonance)
{
    lastCutoff = cutoffHz;
    lastRes = resonance;

    const auto fc = std::clamp ((double) cutoffHz, 10.0, sampleRate * 0.45);
    const auto g = (float) std::tan (3.141592653589793 * fc / sampleRate);
    k = 2.0f - 1.94f * std::clamp (resonance, 0.0f, 1.0f);
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

void SvfFilter::process (float& left, float& right, float cutoffHz, float resonance, FilterType type)
{
    if (cutoffHz != lastCutoff || resonance != lastRes)
        update (cutoffHz, resonance);

    auto run = [&] (float x, int ch)
    {
        auto& s1 = ic1[(size_t) ch];
        auto& s2 = ic2[(size_t) ch];
        const auto v3 = x - s2;
        const auto v1 = a1 * s1 + a2 * v3;
        const auto v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;

        switch (type)
        {
            case FilterType::LowPass:  return v2;
            case FilterType::HighPass: return x - k * v1 - v2;
            case FilterType::BandPass: return v1;
        }
        return v2;
    };

    left = run (left, 0);
    right = run (right, 1);
}

} // namespace batida
