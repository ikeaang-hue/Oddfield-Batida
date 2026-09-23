#pragma once

#include <cmath>

namespace batida
{

// Mono topology-preserving SVF section with all four outputs. Used for the
// Linkwitz-Riley crossover (two Butterworth sections per band), its matching
// allpass, the exciter band and the EQ's high/low-pass.
struct SvfSection
{
    float g = 0.0f, k = 1.414213562f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float s1 = 0.0f, s2 = 0.0f;
    float lp = 0.0f, bp = 0.0f, hp = 0.0f;

    void setCutoff (double hz, double sampleRate, float q = 0.70710678f)
    {
        const auto fc = std::fmin (std::fmax (hz, 10.0), sampleRate * 0.45);
        g = (float) std::tan (3.141592653589793 * fc / sampleRate);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() { s1 = s2 = 0.0f; }

    void tick (float x)
    {
        const auto v3 = x - s2;
        const auto v1 = a1 * s1 + a2 * v3;
        const auto v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
    }

    float allpass (float x) const { return x - 2.0f * k * bp; } // valid after tick (x)
};

// RBJ biquad (direct form I) for the EQ's shelves and bell.
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

    void reset() { x1 = x2 = y1 = y2 = 0.0f; }

    float process (float x)
    {
        const auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }

    enum class Shape { LowShelf, Bell, HighShelf };

    void set (Shape shape, double hz, double sampleRate, float gainDb, float q)
    {
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto w = 2.0 * 3.141592653589793 * hz / sampleRate;
        const auto cw = std::cos (w), sw = std::sin (w);
        const auto alpha = sw / (2.0 * q);
        double nb0, nb1, nb2, na0, na1, na2;

        if (shape == Shape::Bell)
        {
            nb0 = 1 + alpha * A; nb1 = -2 * cw; nb2 = 1 - alpha * A;
            na0 = 1 + alpha / A; na1 = -2 * cw; na2 = 1 - alpha / A;
        }
        else
        {
            const auto sq = 2.0 * std::sqrt (A) * alpha;
            if (shape == Shape::LowShelf)
            {
                nb0 = A * ((A + 1) - (A - 1) * cw + sq);
                nb1 = 2 * A * ((A - 1) - (A + 1) * cw);
                nb2 = A * ((A + 1) - (A - 1) * cw - sq);
                na0 = (A + 1) + (A - 1) * cw + sq;
                na1 = -2 * ((A - 1) + (A + 1) * cw);
                na2 = (A + 1) + (A - 1) * cw - sq;
            }
            else
            {
                nb0 = A * ((A + 1) + (A - 1) * cw + sq);
                nb1 = -2 * A * ((A - 1) + (A + 1) * cw);
                nb2 = A * ((A + 1) + (A - 1) * cw - sq);
                na0 = (A + 1) - (A - 1) * cw + sq;
                na1 = 2 * ((A - 1) - (A + 1) * cw);
                na2 = (A + 1) - (A - 1) * cw - sq;
            }
        }

        b0 = (float) (nb0 / na0);
        b1 = (float) (nb1 / na0);
        b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0);
        a2 = (float) (na2 / na0);
    }
};

} // namespace batida
