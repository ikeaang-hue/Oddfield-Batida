#include "Vary.h"

#include "Engine/ParamRange.h"
#include "Engine/Voice.h"

#include <cmath>

namespace batida
{

namespace
{
bool isOpField (int p, int field)
{
    return p >= vp::OpBase && (p - vp::OpBase) % kNumOpFields == field;
}

// How far each parameter may wander (multiplies the amount); 0 = never.
float spreadOf (int p)
{
    switch (p)
    {
        case vp::FmPitch: case vp::SmpTune: return 0.25f; // pitch: small moves keep the note
        case vp::FltRes:                    return 0.6f;
        case vp::PitchAmt:                  return 0.5f;
        default: break;
    }
    if (isOpField (p, Ratio)) return 0.5f;
    return 1.0f;
}

// Direction weights: which way each parameter moves for a direction.
float directionWeight (int p, VaryDirection d, const VoiceParams& base)
{
    const auto lowPass = (FilterType) base.choice (vp::FltType) == FilterType::LowPass;
    float bright = 0.0f, shortW = 0.0f, noisy = 0.0f, dirty = 0.0f;

    switch (p)
    {
        case vp::FmBright:  bright = 1.0f; break;
        case vp::FltCutoff: bright = lowPass ? 1.0f : 0.0f; break;
        case vp::FltRes:    bright = 0.1f; break;
        case vp::Drive:     bright = 0.2f; dirty = 1.0f; break;
        case vp::Punch:     dirty = 0.3f; break;
        case vp::FmFeedback: noisy = 1.0f; break;
        case vp::FmGrit:    noisy = 0.6f; dirty = 0.3f; break;
        case vp::FmHarm:    noisy = 0.6f; break;
        case vp::AmpD:      shortW = -1.0f; break;
        case vp::AmpR:      shortW = -0.8f; break;
        case vp::PitchDec:  shortW = -0.4f; break;
        default: break;
    }
    if (isOpField (p, OpLevel)) bright = 0.5f;
    if (isOpField (p, Wave))    { bright = 0.3f; noisy = 0.5f; }
    if (isOpField (p, Fold))    { bright = 0.3f; noisy = 0.3f; dirty = 0.5f; }
    if (isOpField (p, OpD))     shortW = -0.6f;
    if (isOpField (p, OpR))     shortW = -0.5f;

    switch (d)
    {
        case VaryDirection::Brighter: return bright;
        case VaryDirection::Darker:   return -bright;
        case VaryDirection::Shorter:  return shortW; // decay weights point "shorter" (negative)
        case VaryDirection::Longer:   return -shortW;
        case VaryDirection::Noisy:    return noisy;
        case VaryDirection::Tonal:    return -noisy;
        case VaryDirection::Dirtier:  return dirty;
        case VaryDirection::Cleaner:  return -dirty;
        case VaryDirection::None:     break;
    }
    return 0.0f;
}

// A distance between two sounds, in rough "just noticeable" units.
float distance (const SoundFeatures& a, const SoundFeatures& b)
{
    const auto level = (a.rmsDb - b.rmsDb) / 6.0f;
    const auto length = std::log2 ((a.lengthMs + 20.0f) / (b.lengthMs + 20.0f)) / 0.5f;
    const auto bright = std::log2 ((a.brightnessHz + 50.0f) / (b.brightnessHz + 50.0f)) / 0.35f;
    return std::sqrt (level * level + length * length + bright * bright);
}

// Did the candidate move the way the direction asked, compared with the base?
bool movedTheRightWay (const SoundFeatures& c, const SoundFeatures& base, VaryDirection d)
{
    switch (d)
    {
        case VaryDirection::Brighter: case VaryDirection::Noisy: case VaryDirection::Dirtier:
            return c.brightnessHz > base.brightnessHz * 1.03f;
        case VaryDirection::Darker: case VaryDirection::Tonal: case VaryDirection::Cleaner:
            return c.brightnessHz < base.brightnessHz * 0.97f;
        case VaryDirection::Shorter: return c.lengthMs < base.lengthMs * 0.95f;
        case VaryDirection::Longer:  return c.lengthMs > base.lengthMs * 1.05f;
        case VaryDirection::None:    return true;
    }
    return true;
}
} // namespace

VaryGroup varyGroupOf (int p)
{
    switch (p)
    {
        case vp::Balance: case vp::FmPitch: case vp::FmFeedback: case vp::FmHarm: case vp::FmBright:
        case vp::FmBrightDecay: case vp::FmGrit: case vp::SmpTune:
            return VaryGroup::Source;
        case vp::Punch: case vp::Drive: case vp::FltCutoff: case vp::FltRes:
            return VaryGroup::Fx;
        case vp::AmpA: case vp::AmpD: case vp::AmpS: case vp::AmpR: case vp::PitchAmt: case vp::PitchDec:
        case vp::SmpFadeOut:
            return VaryGroup::Envelopes;
        default: break;
    }
    if (p >= vp::OpBase)
    {
        const auto f = (p - vp::OpBase) % kNumOpFields;
        if (f == Ratio || f == Freq || f == OpLevel || f == Wave || f == Fold)
            return VaryGroup::Source;
        if (f == OpA || f == OpD || f == OpS || f == OpR)
            return VaryGroup::Envelopes;
    }
    return VaryGroup::None;
}

SoundFeatures measureVoice (const VoiceParams& params, const SampleData* sample, double sampleRate)
{
    constexpr int block = 256;
    const auto total = (int) sampleRate;           // 1 s
    const auto offAt = (int) (0.25 * sampleRate);  // Gate voices: a quarter-second note

    Voice voice;
    voice.prepare (sampleRate);
    voice.setSampleData (sample);
    voice.setParameters (params);
    voice.noteOn (Voice::kBaseKey, 0.85f);

    std::vector<float> l ((size_t) total, 0.0f), r ((size_t) total, 0.0f);
    for (int pos = 0; pos < total; pos += block)
    {
        const auto n = std::min (block, total - pos);
        if (offAt >= pos && offAt < pos + n)
            voice.noteOff (Voice::kBaseKey);
        voice.render (l.data() + pos, r.data() + pos, l.data() + pos, r.data() + pos, n);
        if (! voice.isActive())
            break;
    }

    SoundFeatures f;
    float peak = 0.0f;
    for (int i = 0; i < total; ++i)
    {
        const auto x = std::max (std::abs (l[(size_t) i]), std::abs (r[(size_t) i]));
        if (! std::isfinite (x))
            f.finite = false;
        peak = std::max (peak, x);
    }
    if (peak <= 0.0f || ! f.finite)
        return f;

    int last = 0, crossings = 0;
    double sum = 0.0;
    for (int i = 0; i < total; ++i)
    {
        if (std::abs (l[(size_t) i]) > peak * 0.001f)
            last = i;
        sum += (double) l[(size_t) i] * l[(size_t) i];
        if (i > 0 && (l[(size_t) i - 1] < 0.0f) != (l[(size_t) i] < 0.0f))
            ++crossings;
    }
    const auto active = std::max (1, last);
    f.peakDb = 20.0f * std::log10 (peak);
    f.rmsDb = (float) (10.0 * std::log10 (std::max (1.0e-12, sum / active)));
    f.lengthMs = (float) (last * 1000.0 / sampleRate);
    f.brightnessHz = (float) (crossings * 0.5 * sampleRate / active);
    return f;
}

std::vector<VaryCandidate> vary (const VaryRequest& req)
{
    const auto baseFeatures = measureVoice (req.base, req.sample);
    const auto amount = std::clamp (req.amount, 0.02f, 1.0f);
    juce::Random random ((juce::int64) req.seed);

    auto locked = [&] (VaryGroup g)
    {
        return g == VaryGroup::None || (g == VaryGroup::Source && req.lockSource)
            || (g == VaryGroup::Fx && req.lockFx) || (g == VaryGroup::Envelopes && req.lockEnvelopes);
    };
    auto gaussian = [&]
    {
        // Box-Muller
        const auto u1 = std::max (1.0e-7f, random.nextFloat()), u2 = random.nextFloat();
        return std::sqrt (-2.0f * std::log (u1)) * std::cos (6.2831853f * u2);
    };

    std::vector<VaryCandidate> valid;
    for (int batch = 0; batch < 3 && (int) valid.size() < req.count * 2; ++batch)
    {
        for (int i = 0; i < 12; ++i)
        {
            auto p = req.base;
            for (int k = 0; k < kNumVoiceParams; ++k)
            {
                if (locked (varyGroupOf (k)) || ! isContinuous (voiceParamSpecs()[(size_t) k]))
                    continue;
                const auto& range = voiceRanges()[(size_t) k];
                auto n = range.convertTo0to1 (p[k]);
                n += amount * 0.3f * spreadOf (k) * gaussian();
                n += (0.1f + 0.3f * amount) * directionWeight (k, req.direction, req.base);
                p[k] = range.convertFrom0to1 (std::clamp (n, 0.0f, 1.0f));
            }

            const auto f = measureVoice (p, req.sample);
            if (! f.finite || f.peakDb < -45.0f || f.peakDb > 3.0f)
                continue; // silent or clipping
            if (distance (f, baseFeatures) < 0.35f)
                continue; // sounds like the original
            if (! movedTheRightWay (f, baseFeatures, req.direction))
                continue;
            valid.push_back ({ p, f });
        }
    }

    // Keep the most varied set: farthest-first from the original and each other.
    std::vector<VaryCandidate> chosen;
    while ((int) chosen.size() < req.count && ! valid.empty())
    {
        size_t best = 0;
        float bestScore = -1.0f;
        for (size_t i = 0; i < valid.size(); ++i)
        {
            auto nearest = distance (valid[i].features, baseFeatures);
            for (const auto& c : chosen)
                nearest = std::min (nearest, distance (valid[i].features, c.features));
            // Prefer candidates that differ, but not wildly more than the amount asks.
            const auto score = nearest - std::max (0.0f, distance (valid[i].features, baseFeatures) - (1.0f + 6.0f * amount));
            if (score > bestScore)
            {
                bestScore = score;
                best = i;
            }
        }
        if (bestScore < 0.35f && ! chosen.empty())
            break; // the rest are near-duplicates of what we have
        chosen.push_back (valid[best]);
        valid.erase (valid.begin() + (long) best);
    }
    return chosen;
}

} // namespace batida
