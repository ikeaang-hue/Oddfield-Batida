#pragma once

#include "Parameters.h"

#include <array>

namespace batida
{

// One-pole smoother for control values.
struct Smoother
{
    float value = 0.0f, coef = 0.0f;

    void setTime (float ms, double sampleRate);
    void snap (float v) { value = v; }
    float next (float target) { return value = target + (value - target) * coef; }
};

// Transient shaper: one knob adds attack and tightens the tail. Compares a
// fast and a slow envelope follower; gain rises while the fast one leads
// (attack) and falls while it trails (tail).
class Punch
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float& left, float& right, float amount);

private:
    float fastAttack = 0.0f, fastRelease = 0.0f, slowAttack = 0.0f, slowRelease = 0.0f;
    float fast = 0.0f, slow = 0.0f;
};

// Soft (tanh), hard clip, sine fold, or crush (bit depth + sample rate).
class Drive
{
public:
    void reset();
    void process (float& left, float& right, float amount, DriveType type);

private:
    static float shape (float x, float amount, DriveType type);

    std::array<float, 2> held {};
    int holdCounter = 0;
};

// Topology-preserving state-variable filter (LP/HP/BP), stereo.
class SvfFilter
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float& left, float& right, float cutoffHz, float resonance, FilterType type);

private:
    void update (float cutoffHz, float resonance);

    double sampleRate = 48000.0;
    float lastCutoff = -1.0f, lastRes = -1.0f;
    float k = 2.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    std::array<float, 2> ic1 {}, ic2 {};
};

} // namespace batida
