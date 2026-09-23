#pragma once

namespace batida
{

// ADSR with a linear attack and exponential decay/release. Times are the time
// to fall by 60 dB, which matches how long a drum hit is heard.
//
// One-shot: note-off is ignored. The envelope runs attack → decay (towards
// sustain) → release, as if the note were released when the decay settles.
// With sustain 0 that is a plain AD; with sustain > 0 it is a two-stage decay.
// Gate: a normal ADSR that holds sustain until release().
class Envelope
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void setSampleRate (double newSampleRate) { sampleRate = newSampleRate; }
    void setParameters (float attackMs, float decayMs, float sustainLevel, float releaseMs);

    void trigger (bool oneShot);   // restarts from the current level (no click)
    void release();
    void reset();                  // hard stop at 0

    float next()
    {
        switch (stage)
        {
            case Stage::Idle:
                return 0.0f;

            case Stage::Attack:
                value += attackStep;
                if (value >= 1.0f)
                {
                    value = 1.0f;
                    stage = Stage::Decay;
                }
                return value;

            case Stage::Decay:
                value = sustain + (value - sustain) * decayCoef;
                if (value - sustain < kSettle)
                {
                    value = sustain;
                    if (oneShotMode || sustain <= 0.0f)
                        stage = sustain > 0.0f ? Stage::Release : Stage::Idle;
                    else
                        stage = Stage::Sustain;
                }
                return value;

            case Stage::Sustain:
                value = sustain;
                return value;

            case Stage::Release:
                value *= releaseCoef;
                if (value < kSettle)
                {
                    value = 0.0f;
                    stage = Stage::Idle;
                }
                return value;
        }
        return 0.0f;
    }

    bool isActive() const { return stage != Stage::Idle; }
    Stage getStage() const { return stage; }
    float getValue() const { return value; }

private:
    static constexpr float kSettle = 1.0e-4f; // -80 dB

    static float coefficientFor (float ms, double sampleRate);

    double sampleRate = 48000.0;
    Stage stage = Stage::Idle;
    float value = 0.0f;
    float attackStep = 1.0f, decayCoef = 0.0f, sustain = 0.0f, releaseCoef = 0.0f;
    bool oneShotMode = true;
};

} // namespace batida
