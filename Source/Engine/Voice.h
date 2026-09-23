#pragma once

#include "Envelope.h"
#include "FmSource.h"
#include "Parameters.h"
#include "Processors.h"
#include "SampleSource.h"

namespace batida
{

// One monophonic voice: source (FM | Sample | Layer) → amp → punch → drive →
// filter → level/pan. A new note retriggers. With glide above 0, a note that
// arrives while the voice sounds slides from the current pitch; otherwise the
// old note is faded out over ~1.5 ms and the new one starts from a clean phase.
class Voice
{
public:
    static constexpr float kBaseHz = 261.6256f; // FM Pitch 0 = C3
    static constexpr int kBaseKey = 60;         // key that plays at the voice's own pitch

    void prepare (double sampleRate);
    void setParameters (const VoiceParams& p);
    void setSampleData (const SampleData* data) { sampleSource.setData (data); }

    void noteOn (int key, float velocity); // velocity 0..1
    void noteOff (int key);
    void allNotesOff();                    // release, or hard stop if reset
    void reset();

    // Adds this voice into the buffers.
    void render (float* left, float* right, int numSamples);

    bool isActive() const { return active; }
    float getPitchOffset() const { return pitch; } // semitones from the base key, for tests

private:
    void startNote (int key, float velocity);
    void removeHeld (int key);
    bool sourceActive() const;

    double sampleRate = 48000.0;
    VoiceParams params;
    SourceMode mode = SourceMode::FM;
    bool oneShot = true;

    FmSource fm;
    SampleSource sampleSource;
    Envelope ampEnv, pitchEnv;
    Punch punch;
    Drive drive;
    SvfFilter filter;

    Smoother gainL, gainR, logCutoff;
    std::array<float, 2> gainTargets { 1.0f, 1.0f };
    float cutoffTarget = 14.29f; // log2 Hz
    float cutoffHz = 20000.0f;

    bool active = false;
    float pitch = 0.0f, targetPitch = 0.0f, glideCoef = 0.0f;
    float velocityGain = 1.0f;
    float fmGain = 1.0f, sampleGain = 1.0f;

    bool stealing = false;
    float stealGain = 1.0f, stealStep = 0.0f;
    int pendingKey = kBaseKey;
    float pendingVelocity = 1.0f;

    std::array<int, 16> held {};
    int numHeld = 0;
};

} // namespace batida
