#pragma once

#include <array>

namespace batida
{

// The kit distortion's shaper: five character anchors blended continuously
// across Type (0..1): tape → tube → clip → fold → crush/decimate. Runs at the
// chain's oversampled rate. Output level is compensated from a table measured
// once at startup, so drive and type change character rather than loudness.
class Saturator
{
public:
    static constexpr int kNumShapes = 5;
    static constexpr int kOversampling = 4; // crush hold lengths are in 1x samples

    void reset();

    // drive 0..1 (0 = clean), type 0..1. Level compensated.
    float process (float x, float drive, float type, int channel);

    // Loudness correction for a drive/type pair (bilinear lookup).
    static float compensation (float drive, float type);

private:
    float shape (int index, float x, float drive, int channel);

    std::array<float, 2> held {};
    std::array<int, 2> holdCounter {};
};

} // namespace batida
