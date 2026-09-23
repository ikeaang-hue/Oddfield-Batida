#include "Voice.h"

#include <algorithm>
#include <cmath>

namespace batida
{

namespace
{
float dbToGain (float db) { return db <= -60.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }
} // namespace

void Voice::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    fm.prepare (sampleRate);
    sampleSource.prepare (sampleRate);
    ampEnv.setSampleRate (sampleRate);
    pitchEnv.setSampleRate (sampleRate);
    punch.prepare (sampleRate);
    filter.prepare (sampleRate);
    gainL.setTime (5.0f, sampleRate);
    gainR.setTime (5.0f, sampleRate);
    logCutoff.setTime (10.0f, sampleRate);
    stealStep = (float) (1.0 / (0.0015 * sampleRate));
    reset();
}

void Voice::setParameters (const VoiceParams& p)
{
    const bool first = ! active && ! stealing;
    params = p;
    mode = (SourceMode) std::clamp (p.choice (vp::SrcMode), 0, 2);
    oneShot = (PlayMode) p.choice (vp::PlayMode) == PlayMode::OneShot;

    fm.setParameters (computeEffectiveFm (p));
    ampEnv.setParameters (p[vp::AmpA], p[vp::AmpD], p[vp::AmpS], p[vp::AmpR]);
    pitchEnv.setParameters (0.0f, p[vp::PitchDec], 0.0f, p[vp::PitchDec]);

    SampleSource::Settings s;
    s.start = p[vp::SmpStart];
    s.end = p[vp::SmpEnd];
    s.loopStart = p[vp::SmpLoopStart];
    s.loopEnd = p[vp::SmpLoopEnd];
    s.fadeInMs = p[vp::SmpFadeIn];
    s.fadeOutMs = p[vp::SmpFadeOut];
    s.gain = dbToGain (p[vp::SmpGain]);
    s.reverse = p.flag (vp::SmpReverse);
    s.loop = p.flag (vp::SmpLoop);
    s.gate = ! oneShot;
    sampleSource.setSettings (s);

    const auto balance = std::clamp (p[vp::Balance], 0.0f, 1.0f);
    fmGain = mode == SourceMode::Layer ? std::min (1.0f, 2.0f * balance) : 1.0f;
    sampleGain = mode == SourceMode::Layer ? std::min (1.0f, 2.0f * (1.0f - balance)) : 1.0f;

    const auto glideMs = p[vp::Glide];
    glideCoef = glideMs <= 0.0f ? 0.0f : (float) std::exp (-4.6 / (glideMs * 0.001 * sampleRate));

    // Equal-power pan, unity at centre.
    const auto level = dbToGain (p[vp::Level]);
    const auto angle = (std::clamp (p[vp::Pan], -1.0f, 1.0f) + 1.0f) * 0.785398163f;
    const auto targetL = level * std::cos (angle) * 1.414213562f;
    const auto targetR = level * std::sin (angle) * 1.414213562f;
    const auto targetCutoff = std::log2 (std::clamp (p[vp::FltCutoff], 20.0f, 20000.0f));

    if (first)
    {
        gainL.snap (targetL);
        gainR.snap (targetR);
        logCutoff.snap (targetCutoff);
        cutoffHz = std::exp2 (targetCutoff);
    }
    gainTargets[0] = targetL;
    gainTargets[1] = targetR;
    cutoffTarget = targetCutoff;
}

void Voice::removeHeld (int key)
{
    const auto end = held.begin() + numHeld;
    numHeld = (int) (std::remove (held.begin(), end, key) - held.begin());
}

void Voice::noteOn (int key, float velocity)
{
    removeHeld (key);
    if (numHeld == (int) held.size())
    {
        std::move (held.begin() + 1, held.end(), held.begin());
        --numHeld;
    }
    held[(size_t) numHeld++] = key;

    if (! active)
    {
        startNote (key, velocity);
    }
    else if (glideCoef > 0.0f)
    {
        // Slide: retrigger the envelopes from where they are, keep the phase.
        targetPitch = (float) (key - kBaseKey);
        velocityGain = 1.0f - params[vp::VelSens] * (1.0f - velocity * velocity);
        ampEnv.trigger (oneShot);
        pitchEnv.trigger (true);
        fm.trigger (oneShot, false);
        sampleSource.trigger();
    }
    else
    {
        stealing = true;
        stealGain = 1.0f;
        pendingKey = key;
        pendingVelocity = velocity;
    }
}

void Voice::noteOff (int key)
{
    const bool wasCurrent = numHeld > 0 && held[(size_t) numHeld - 1] == key;
    removeHeld (key);

    if (numHeld > 0)
    {
        // Return to the previous held note without retriggering.
        if (wasCurrent)
            targetPitch = (float) (held[(size_t) numHeld - 1] - kBaseKey);
        return;
    }

    if (! oneShot && ! stealing)
    {
        ampEnv.release();
        fm.release();
    }
}

void Voice::allNotesOff()
{
    numHeld = 0;
    if (! oneShot)
    {
        ampEnv.release();
        fm.release();
    }
}

void Voice::reset()
{
    active = stealing = false;
    numHeld = 0;
    fm.reset();
    sampleSource.reset();
    ampEnv.reset();
    pitchEnv.reset();
    punch.reset();
    drive.reset();
    filter.reset();
}

void Voice::startNote (int key, float velocity)
{
    targetPitch = pitch = (float) (key - kBaseKey);
    velocityGain = 1.0f - params[vp::VelSens] * (1.0f - velocity * velocity);

    ampEnv.reset();
    pitchEnv.reset();
    punch.reset();
    drive.reset();
    filter.reset();

    ampEnv.trigger (oneShot);
    pitchEnv.trigger (true);
    fm.trigger (oneShot, true);
    sampleSource.trigger();
    active = true;

    // A gate note that was released while the old note faded out.
    if (! oneShot && numHeld == 0)
    {
        ampEnv.release();
        fm.release();
    }
}

bool Voice::sourceActive() const
{
    return (mode != SourceMode::Sample && fm.isActive())
        || (mode != SourceMode::FM && sampleSource.isActive());
}

void Voice::render (float* left, float* right, int numSamples)
{
    if (! active)
        return;

    const auto pitchAmt = params[vp::PitchAmt];
    const auto fmPitch = params[vp::FmPitch];
    const auto smpTune = params[vp::SmpTune];
    const auto punchAmt = params[vp::Punch];
    const auto driveAmt = params[vp::Drive];
    const auto driveType = (DriveType) std::clamp (params.choice (vp::DriveType), 0, 3);
    const auto filterType = (FilterType) std::clamp (params.choice (vp::FltType), 0, 2);
    const auto resonance = params[vp::FltRes];
    const bool useFm = mode != SourceMode::Sample;
    const bool useSample = mode != SourceMode::FM;

    for (int i = 0; i < numSamples; ++i)
    {
        auto steal = 1.0f;
        if (stealing)
        {
            stealGain -= stealStep;
            if (stealGain <= 0.0f)
            {
                stealing = false;
                startNote (pendingKey, pendingVelocity);
            }
            else
            {
                steal = stealGain;
            }
        }

        pitch = targetPitch + (pitch - targetPitch) * glideCoef;
        const auto semis = pitch + pitchEnv.next() * pitchAmt;

        float l = 0.0f, r = 0.0f;

        if (useFm)
        {
            const auto x = fm.process (kBaseHz * std::exp2 ((fmPitch + semis) * (1.0f / 12.0f))) * fmGain;
            l += x;
            r += x;
        }

        if (useSample)
        {
            float sl, sr;
            sampleSource.process (std::exp2 ((smpTune + semis) * (1.0f / 12.0f)), sl, sr);
            l += sl * sampleGain;
            r += sr * sampleGain;
        }

        const auto amp = ampEnv.next() * velocityGain * steal;
        l *= amp;
        r *= amp;

        punch.process (l, r, punchAmt);
        drive.process (l, r, driveAmt, driveType);

        const auto lc = logCutoff.value;
        if (std::abs (logCutoff.next (cutoffTarget) - lc) > 1.0e-5f)
            cutoffHz = std::exp2 (logCutoff.value);

        if (filterType != FilterType::LowPass || cutoffHz < 19900.0f)
            filter.process (l, r, cutoffHz, resonance, filterType);

        left[i] += l * gainL.next (gainTargets[0]);
        right[i] += r * gainR.next (gainTargets[1]);

        if (! ampEnv.isActive() || ! sourceActive())
        {
            if (stealing)
            {
                stealing = false;
                startNote (pendingKey, pendingVelocity);
            }
            else
            {
                active = false;
                return;
            }
        }
    }
}

} // namespace batida
