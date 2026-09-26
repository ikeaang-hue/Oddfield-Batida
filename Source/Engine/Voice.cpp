#include "Voice.h"

#include <algorithm>
#include <cmath>

namespace batida
{

namespace
{
float dbToGain (float db) { return db <= -60.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }

// Stack: each copy's offset in semitones, per interval (copy 0 is the root).
constexpr std::array<std::array<float, kMaxStack>, 6> kStackShapes { {
    { 0.0f, 0.0f, 0.0f, 0.0f },    // Unison
    { 0.0f, 12.0f, 0.0f, 12.0f },  // Octave
    { 0.0f, 7.0f, 12.0f, 19.0f },  // Fifth
    { 0.0f, 3.0f, 7.0f, 12.0f },   // Minor
    { 0.0f, 3.0f, 7.0f, 10.0f },   // Minor 7
    { 0.0f, 4.0f, 7.0f, 12.0f },   // Major
} };

// Where each copy sits, left to right (detune follows the same places): the
// root nearest the centre, so a chord stays anchored and in tune.
constexpr std::array<std::array<int, kMaxStack>, kMaxStack> kStackPlaces { {
    { 0, 0, 0, 0 }, { 0, 1, 0, 0 }, { 1, 0, 2, 0 }, { 1, 2, 0, 3 },
} };

// Copies start at different phases so they don't add up in phase on the attack.
constexpr std::array<float, kMaxStack> kStackPhases { 0.0f, 0.37f, 0.71f, 0.19f };
} // namespace

void Voice::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    fm.prepare (2.0 * sampleRate);
    sampleSource.prepare (sampleRate);
    for (size_t k = 0; k < stackFm.size(); ++k)
    {
        stackFm[k].prepare (2.0 * sampleRate);
        stackFm[k].setPhaseOffset (kStackPhases[k + 1]);
        stackSample[k].prepare (sampleRate);
    }
    ampEnv.setSampleRate (sampleRate);
    pitchEnv.setSampleRate (sampleRate);
    punch.prepare (sampleRate);
    filter.prepare (sampleRate);
    gainL.setTime (5.0f, sampleRate);
    gainR.setTime (5.0f, sampleRate);
    logCutoff.setTime (10.0f, sampleRate);
    chainAmt.setTime (10.0f, sampleRate);
    audibleGain.setTime (3.0f, sampleRate);
    audibleGain.snap (audibleTarget);
    stealStep = (float) (1.0 / (0.0015 * sampleRate));
    reset();
}

void Voice::setParameters (const VoiceParams& p)
{
    const bool first = ! active && ! stealing;
    params = p;
    mode = (SourceMode) std::clamp (p.choice (vp::SrcMode), 0, 2);
    oneShot = (PlayMode) p.choice (vp::PlayMode) == PlayMode::OneShot;

    const auto fmEffective = computeEffectiveFm (p);
    fm.setParameters (fmEffective);
    for (auto& f : stackFm)
        f.setParameters (fmEffective);
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
    s.slices.mode = (SliceMode) std::clamp (p.choice (vp::SmpSliceMode), 0, 2);
    s.slices.gridCount = (int) std::lround (p[vp::SmpSlices]);
    s.slices.sensitivity = p[vp::SmpSliceSens];
    sampleSource.setSettings (s);
    for (auto& copy : stackSample)
        copy.setSettings (s);

    // Stack: pitch and pan of each copy; the sum keeps the level of one copy
    // (copies drift apart, so they add up in power, not in amplitude).
    const auto count = std::clamp (p.choice (vp::StackCount) + 1, 1, kMaxStack);
    if (count > 1 && stackCount == 1)
    {
        stackDecimatorL.reset();
        stackDecimatorR.reset();
    }
    stackCount = count;
    const auto& shape = kStackShapes[(size_t) std::clamp (p.choice (vp::StackInterval), 0, (int) kStackShapes.size() - 1)];
    const auto detuneCents = std::clamp (p[vp::StackDetune], 0.0f, 100.0f);
    const auto spread = std::clamp (p[vp::StackSpread], 0.0f, 1.0f);
    const auto norm = 1.0f / std::sqrt ((float) count);
    for (int k = 0; k < count; ++k)
    {
        const auto place = kStackPlaces[(size_t) count - 1][(size_t) k];
        const auto position = count > 1 ? 2.0f * (float) place / (float) (count - 1) - 1.0f : 0.0f; // -1..1
        stackRatio[(size_t) k] = std::exp2 ((shape[(size_t) k] + detuneCents * position * 0.01f) / 12.0f);
        const auto angle = (spread * position + 1.0f) * 0.785398163f;
        stackL[(size_t) k] = std::cos (angle) * 1.414213562f * norm;
        stackR[(size_t) k] = std::sin (angle) * 1.414213562f * norm;
    }

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
        chainAmt.snap (std::clamp (p[vp::ChainAmt], 0.0f, 1.0f));
        audibleGain.snap (audibleTarget);
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

void Voice::noteOn (int key, float velocity, int slice)
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
        startNote (key, velocity, slice);
    }
    else if (glideCoef > 0.0f)
    {
        // Slide: retrigger the envelopes from where they are, keep the phase.
        targetPitch = (float) (key - kBaseKey);
        velocityGain = 1.0f - params[vp::VelSens] * (1.0f - velocity * velocity);
        ampEnv.trigger (oneShot);
        pitchEnv.trigger (true);
        triggerSources (false, slice);
    }
    else
    {
        stealing = true;
        stealGain = 1.0f;
        pendingKey = key;
        pendingVelocity = velocity;
        pendingSlice = slice;
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
        releaseSources();
    }
}

void Voice::allNotesOff()
{
    numHeld = 0;
    if (! oneShot)
    {
        ampEnv.release();
        releaseSources();
    }
}

void Voice::choke()
{
    if (! active)
        return;
    stealing = true;
    stealGain = std::min (stealGain, 1.0f);
    pendingKey = -1; // nothing to start afterwards
    numHeld = 0;
}

void Voice::reset()
{
    active = stealing = false;
    numHeld = 0;
    fm.reset();
    fmDecimator.reset();
    sampleSource.reset();
    for (size_t k = 0; k < stackFm.size(); ++k)
    {
        stackFm[k].reset();
        stackSample[k].reset();
    }
    stackDecimatorL.reset();
    stackDecimatorR.reset();
    ampEnv.reset();
    pitchEnv.reset();
    punch.reset();
    drive.reset();
    filter.reset();
}

void Voice::startNote (int key, float velocity, int slice)
{
    targetPitch = pitch = (float) (key - kBaseKey);
    velocityGain = 1.0f - params[vp::VelSens] * (1.0f - velocity * velocity);

    // The old note has faded to silence; clear everything that could carry it over.
    ampEnv.reset();
    pitchEnv.reset();
    punch.reset();
    drive.reset();
    filter.reset();
    fmDecimator.reset();
    stackDecimatorL.reset();
    stackDecimatorR.reset();

    ampEnv.trigger (oneShot);
    pitchEnv.trigger (true);
    triggerSources (true, slice);
    active = true;

    // A gate note that was released while the old note faded out.
    if (! oneShot && numHeld == 0)
    {
        ampEnv.release();
        releaseSources();
    }
}

void Voice::triggerSources (bool resetPhase, int slice)
{
    fm.trigger (oneShot, resetPhase);
    sampleSource.trigger (slice);
    for (int k = 1; k < stackCount; ++k)
    {
        stackFm[(size_t) k - 1].trigger (oneShot, resetPhase);
        stackSample[(size_t) k - 1].trigger (slice);
    }
}

void Voice::releaseSources()
{
    fm.release();
    for (auto& f : stackFm)
        f.release();
}

bool Voice::sourceActive() const
{
    if ((mode != SourceMode::Sample && fm.isActive()) || (mode != SourceMode::FM && sampleSource.isActive()))
        return true;
    // A copy detuned down plays a sample a little longer than the root.
    for (int k = 1; k < stackCount; ++k)
        if (mode != SourceMode::FM && stackSample[(size_t) k - 1].isActive())
            return true;
    return false;
}

void Voice::render (float* dryL, float* dryR, float* wetL, float* wetR, int numSamples)
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
    const auto chainTarget = std::clamp (params[vp::ChainAmt], 0.0f, 1.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        auto steal = 1.0f;
        if (stealing)
        {
            stealGain -= stealStep;
            if (stealGain <= 0.0f)
            {
                stealing = false;
                if (pendingKey < 0) // choked
                {
                    reset();
                    return;
                }
                startNote (pendingKey, pendingVelocity, pendingSlice);
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
            const auto hz = kBaseHz * std::exp2 ((fmPitch + semis) * (1.0f / 12.0f));
            if (stackCount == 1)
            {
                const auto a = fm.process (hz);
                const auto x = fmDecimator.process (a, fm.process (hz)) * fmGain;
                l += x;
                r += x;
            }
            else
            {
                // Each copy at 2x, panned, then one decimator per side.
                float l0 = 0.0f, l1 = 0.0f, r0 = 0.0f, r1 = 0.0f;
                for (int k = 0; k < stackCount; ++k)
                {
                    auto& src = k == 0 ? fm : stackFm[(size_t) k - 1];
                    const auto h = hz * stackRatio[(size_t) k];
                    const auto a0 = src.process (h);
                    const auto a1 = src.process (h);
                    l0 += a0 * stackL[(size_t) k];
                    l1 += a1 * stackL[(size_t) k];
                    r0 += a0 * stackR[(size_t) k];
                    r1 += a1 * stackR[(size_t) k];
                }
                l += stackDecimatorL.process (l0, l1) * fmGain;
                r += stackDecimatorR.process (r0, r1) * fmGain;
            }
        }

        if (useSample)
        {
            const auto rate = std::exp2 ((smpTune + semis) * (1.0f / 12.0f));
            if (stackCount == 1)
            {
                float sl, sr;
                sampleSource.process (rate, sl, sr);
                l += sl * sampleGain;
                r += sr * sampleGain;
            }
            else
            {
                for (int k = 0; k < stackCount; ++k)
                {
                    auto& src = k == 0 ? sampleSource : stackSample[(size_t) k - 1];
                    float sl, sr;
                    src.process (rate * stackRatio[(size_t) k], sl, sr);
                    l += sl * sampleGain * stackL[(size_t) k];
                    r += sr * sampleGain * stackR[(size_t) k];
                }
            }
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

        auto audible = audibleGain.next (audibleTarget);
        if (audibleTarget == 0.0f && audible < 1.0e-5f) // fully muted: true silence
            audible = audibleGain.value = 0.0f;
        l *= gainL.next (gainTargets[0]) * audible;
        r *= gainR.next (gainTargets[1]) * audible;
        const auto a = chainAmt.next (chainTarget);
        dryL[i] += l * (1.0f - a);
        dryR[i] += r * (1.0f - a);
        wetL[i] += l * a;
        wetR[i] += r * a;

        if (! ampEnv.isActive() || ! sourceActive())
        {
            if (stealing && pendingKey >= 0)
            {
                stealing = false;
                startNote (pendingKey, pendingVelocity, pendingSlice);
            }
            else
            {
                active = stealing = false;
                return;
            }
        }
    }
}

} // namespace batida
