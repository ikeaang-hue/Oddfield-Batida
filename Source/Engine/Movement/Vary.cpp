#include "Vary.h"

#include "Engine/ParamRange.h"
#include "Engine/Voice.h"

#include <juce_dsp/juce_dsp.h>

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
    const auto sampleOnly = (SourceMode) base.choice (vp::SrcMode) == SourceMode::Sample;
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
        case vp::SmpFadeOut: shortW = 0.5f; break; // a longer fade ends the sound sooner
        default: break;
    }
    // A sample has no FM settings to make it noisier or more tonal: drive and
    // an opener filter make it noisier; resonance and a closed filter, tonal.
    if (sampleOnly)
        switch (p)
        {
            case vp::Drive:     noisy += 0.5f; break;
            case vp::FltCutoff: noisy += lowPass ? 0.4f : 0.0f; break;
            case vp::FltRes:    noisy -= 0.4f; break;
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

float distance (const SoundFeatures& a, const SoundFeatures& b) { return soundDistance (a, b); }

// Did the candidate move the way the direction asked, compared with the base?
// `brightMargin` and `lengthMargin` are the smallest moves that count.
bool movedTheRightWay (const SoundFeatures& c, const SoundFeatures& base, VaryDirection d, float brightMargin, float lengthMargin)
{
    switch (d)
    {
        case VaryDirection::Brighter: case VaryDirection::Noisy: case VaryDirection::Dirtier:
            return c.brightnessHz > base.brightnessHz * (1.0f + brightMargin);
        case VaryDirection::Darker: case VaryDirection::Tonal: case VaryDirection::Cleaner:
            return c.brightnessHz < base.brightnessHz * (1.0f - brightMargin);
        case VaryDirection::Shorter: return c.lengthMs < base.lengthMs * (1.0f - lengthMargin);
        case VaryDirection::Longer:  return c.lengthMs > base.lengthMs * (1.0f + lengthMargin);
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

std::vector<float> renderPeaks (const VoiceParams& params, const SampleData* sample, int bins, double seconds, double sampleRate)
{
    constexpr int block = 256;
    const auto total = std::max (bins, (int) (seconds * sampleRate));
    const auto offAt = (int) (0.25 * sampleRate);

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

    std::vector<float> peaks ((size_t) bins, 0.0f);
    for (int i = 0; i < total; ++i)
    {
        const auto x = std::max (std::abs (l[(size_t) i]), std::abs (r[(size_t) i]));
        auto& p = peaks[(size_t) std::min (bins - 1, i * bins / total)];
        if (std::isfinite (x))
            p = std::max (p, x);
    }
    return peaks;
}

SoundFeatures measureVoice (const VoiceParams& params, const SampleData* sample, double sampleRate)
{
    constexpr int block = 256;
    const auto total = (int) (4.0 * sampleRate);   // long enough for basses and open hats; stops when silent
    const auto offAt = (int) (0.25 * sampleRate);  // Gate voices: a quarter-second note

    Voice voice;
    voice.prepare (sampleRate);
    voice.setSampleData (sample);
    voice.setParameters (params);
    voice.noteOn (Voice::kBaseKey, 0.85f);

    std::vector<float> l ((size_t) total, 0.0f), r ((size_t) total, 0.0f);
    int rendered = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const auto n = std::min (block, total - pos);
        if (offAt >= pos && offAt < pos + n)
            voice.noteOff (Voice::kBaseKey);
        voice.render (l.data() + pos, r.data() + pos, l.data() + pos, r.data() + pos, n);
        rendered = pos + n;
        if (! voice.isActive())
            break;
    }

    SoundFeatures f;
    float peak = 0.0f;
    for (int i = 0; i < rendered; ++i)
    {
        const auto x = std::max (std::abs (l[(size_t) i]), std::abs (r[(size_t) i]));
        if (! std::isfinite (x))
            f.finite = false;
        peak = std::max (peak, x);
    }
    if (peak <= 0.0f || ! f.finite)
        return f;

    // Loudest 50 ms window (both channels).
    const auto window = (int) (0.05 * sampleRate);
    double windowSum = 0.0, loudest = 0.0;
    for (int i = 0; i < rendered; ++i)
    {
        windowSum += 0.5 * ((double) l[(size_t) i] * l[(size_t) i] + (double) r[(size_t) i] * r[(size_t) i]);
        if (i >= window)
            windowSum -= 0.5 * ((double) l[(size_t) (i - window)] * l[(size_t) (i - window)]
                                + (double) r[(size_t) (i - window)] * r[(size_t) (i - window)]);
        loudest = std::max (loudest, windowSum);
    }
    f.loudnessDb = (float) (10.0 * std::log10 (std::max (1.0e-12, loudest / window)));

    int last = 0;
    double sum = 0.0;
    for (int i = 0; i < rendered; ++i)
    {
        if (std::abs (l[(size_t) i]) > peak * 0.001f)
            last = i;
        sum += (double) l[(size_t) i] * l[(size_t) i];
    }
    const auto active = std::max (1, last);
    f.peakDb = 20.0f * std::log10 (peak);
    f.rmsDb = (float) (10.0 * std::log10 (std::max (1.0e-12, sum / active)));
    f.lengthMs = (float) (last * 1000.0 / sampleRate);

    // The spectrum of the hit (its first 2 s), energy-weighted over time:
    // its centroid is the brightness; 12 bands give its shape, so changes of
    // timbre at the same brightness and length still count.
    constexpr int order = 11, size = 1 << order, hop = size / 2;
    static thread_local juce::dsp::FFT fft (order);
    static thread_local juce::dsp::WindowingFunction<float> hann ((size_t) size, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<float> frame ((size_t) (2 * size)), spectrum ((size_t) (size / 2 + 1), 0.0f);
    const auto upTo = std::min (active + 1, (int) (2.0 * sampleRate));
    for (int start = 0; start < upTo; start += hop)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);
        for (int i = 0; i < size && start + i < upTo; ++i)
            frame[(size_t) i] = 0.5f * (l[(size_t) (start + i)] + r[(size_t) (start + i)]);
        hann.multiplyWithWindowingTable (frame.data(), (size_t) size);
        fft.performFrequencyOnlyForwardTransform (frame.data(), true);
        for (size_t k = 0; k < spectrum.size(); ++k)
            spectrum[k] += frame[k] * frame[k];
    }
    double weighted = 0.0, energy = 0.0;
    std::array<double, kSpectrumBands> bands {};
    const auto binHz = sampleRate / size;
    for (size_t k = 1; k < spectrum.size(); ++k)
    {
        const auto hz = (double) k * binHz;
        weighted += hz * spectrum[k];
        energy += spectrum[k];
        if (hz >= 40.0 && hz < 16000.0)
        {
            const auto b = (int) (std::log (hz / 40.0) / std::log (16000.0 / 40.0) * kSpectrumBands);
            bands[(size_t) std::clamp (b, 0, kSpectrumBands - 1)] += spectrum[k];
        }
    }
    f.brightnessHz = energy > 0.0 ? (float) (weighted / energy) : 0.0f;
    for (int b = 0; b < kSpectrumBands; ++b)
        f.bandsDb[(size_t) b] = energy > 0.0 ? (float) std::max (-60.0, 10.0 * std::log10 (bands[(size_t) b] / energy + 1.0e-9)) : -60.0f;
    return f;
}

float soundDistance (const SoundFeatures& a, const SoundFeatures& b)
{
    const auto level = (a.loudnessDb - b.loudnessDb) / 6.0f;
    const auto length = std::log2 ((a.lengthMs + 20.0f) / (b.lengthMs + 20.0f)) / 0.5f;
    const auto bright = std::log2 ((a.brightnessHz + 50.0f) / (b.brightnessHz + 50.0f)) / 0.35f;

    // The spectrum's shape: the RMS change of the bands that hold any energy, per 5 dB.
    double sq = 0.0;
    int n = 0;
    for (int k = 0; k < kSpectrumBands; ++k)
    {
        const auto x = a.bandsDb[(size_t) k], y = b.bandsDb[(size_t) k];
        if (std::max (x, y) < -40.0f)
            continue;
        const auto d = std::clamp (x - y, -20.0f, 20.0f);
        sq += (double) d * d;
        ++n;
    }
    const auto shape = n > 0 ? (float) std::sqrt (sq / n) / 5.0f : 0.0f;
    return std::sqrt (level * level + length * length + bright * bright + shape * shape);
}

bool isAudibleSetting (const VoiceParams& base, int k)
{
    const auto source = base.choice (vp::SrcMode); // 0 FM, 1 Sample, 2 Layer
    const auto isSample = (k >= vp::SmpTune && k <= vp::SmpSliceSens) || k == vp::Balance;
    const auto isFm = k >= vp::FmPitch && k < vp::SmpTune;
    const auto isOp = k >= vp::OpBase;
    if (isSample && source == 0)
        return false;
    if ((isFm || isOp) && source == 1)
        return false;
    if (k == vp::Balance && source != 2)
        return false;
    if ((k == vp::StackDetune || k == vp::StackSpread) && base.choice (vp::StackCount) == 0)
        return false; // one copy: nothing to detune or spread
    if (isOp)
    {
        // A silent operator's settings do nothing (its level can still rise).
        const auto op = (k - vp::OpBase) / kNumOpFields;
        const auto field = (k - vp::OpBase) % kNumOpFields;
        if (field != OpLevel && base.op (op, OpLevel) <= 1.0e-3f)
            return false;
    }
    return true;
}

std::vector<VaryCandidate> vary (const VaryRequest& req)
{
    const auto baseFeatures = measureVoice (req.base, req.sample);
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

    // Only settings this sound can hear are nudged, so no try is wasted.
    std::vector<int> free;
    for (int k = 0; k < kNumVoiceParams; ++k)
        if (! locked (varyGroupOf (k)) && isContinuous (voiceParamSpecs()[(size_t) k]) && isAudibleSetting (req.base, k))
            free.push_back (k);
    if (free.empty())
        return {};

    // Level matching: Level can rise to its maximum; a candidate that would need
    // more than a little beyond that is dropped rather than left quiet.
    const auto baseLevel = req.base[vp::Level];
    const auto& levelSpec = voiceParamSpecs()[(size_t) vp::Level];

    // When a pass finds too little, the next one reaches further, then asks
    // less: smaller moves count, and a little more level shortfall is taken.
    struct Stage
    {
        float amountScale, minDistance, brightMargin, lengthMargin, shortfallDb;
        int batches;
    };
    static constexpr Stage stages[] = {
        { 1.0f, 0.35f, 0.03f, 0.05f, 1.5f, 3 },
        { 1.6f, 0.35f, 0.03f, 0.05f, 1.5f, 2 },
        { 1.6f, 0.2f,  0.01f, 0.02f, 3.0f, 3 },
    };

    std::vector<VaryCandidate> valid;
    float minDistance = stages[0].minDistance;
    for (const auto& stage : stages)
    {
        if ((int) valid.size() >= req.count)
            break;
        minDistance = stage.minDistance;
        const auto amount = std::clamp (req.amount * stage.amountScale, 0.02f, 1.0f);
        for (int batch = 0; batch < stage.batches && (int) valid.size() < req.count * 2; ++batch)
        {
            for (int i = 0; i < 12; ++i)
            {
                auto p = req.base;
                for (const auto k : free)
                {
                    const auto& range = voiceRanges()[(size_t) k];
                    auto n = range.convertTo0to1 (p[k]);
                    n += amount * 0.3f * spreadOf (k) * gaussian();
                    n += (0.1f + 0.3f * amount) * directionWeight (k, req.direction, req.base);
                    p[k] = range.convertFrom0to1 (std::clamp (n, 0.0f, 1.0f));
                }

                auto f = measureVoice (p, req.sample);
                if (! f.finite || f.peakDb < -45.0f)
                    continue; // broken or silent

                const auto wanted = baseLevel + (baseFeatures.loudnessDb - f.loudnessDb);
                const auto level = std::clamp (wanted, levelSpec.min, levelSpec.max);
                if (wanted - level > stage.shortfallDb)
                    continue; // can't be brought up to the original's loudness
                const auto gainDb = level - baseLevel;
                p[vp::Level] = level;
                f.peakDb += gainDb; // Level is a plain gain after everything else
                f.rmsDb += gainDb;
                f.loudnessDb += gainDb;
                if (f.peakDb > 6.0f)
                    continue; // would slam the safety clip

                if (distance (f, baseFeatures) < stage.minDistance)
                    continue; // sounds like the original
                if (! movedTheRightWay (f, baseFeatures, req.direction, stage.brightMargin, stage.lengthMargin))
                    continue;
                valid.push_back ({ p, f, gainDb, {} });
            }
        }
    }

    // Keep the most varied set: farthest-first from the original and each other.
    const auto amount = std::clamp (req.amount, 0.02f, 1.0f);
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
        if (bestScore < minDistance && ! chosen.empty())
            break; // the rest are near-duplicates of what we have
        chosen.push_back (valid[best]);
        valid.erase (valid.begin() + (long) best);
    }
    for (auto& c : chosen)
        c.note = describeChanges (req.base, c.params);
    return chosen;
}

std::string describeChanges (const VoiceParams& base, const VoiceParams& changed)
{
    struct Change { float size; std::string text; };
    std::vector<Change> changes;

    // Only what can be heard (an operator that was silent and now sounds counts).
    auto audible = [&] (int k) { return isAudibleSetting (base, k) || isAudibleSetting (changed, k); };

    for (int k = 0; k < kNumVoiceParams; ++k)
    {
        const auto& spec = voiceParamSpecs()[(size_t) k];
        if (k == vp::Level || ! isContinuous (spec) || ! audible (k))
            continue;
        const auto& range = voiceRanges()[(size_t) k];
        const auto size = std::abs (range.convertTo0to1 (changed[k]) - range.convertTo0to1 (base[k]));
        if (size < 0.01f)
            continue;

        // Times and frequencies change by a ratio; everything else by its share of the range.
        float percent;
        if ((spec.unit == "ms" || spec.unit == "Hz" || spec.unit == "x") && std::abs (base[k]) > 1.0e-6f)
            percent = (changed[k] / base[k] - 1.0f) * 100.0f;
        else
            percent = (changed[k] - base[k]) / std::max (1.0e-6f, spec.max - spec.min) * 100.0f;
        const auto rounded = (int) std::lround (percent);
        if (rounded == 0)
            continue;

        auto label = juce::String (spec.label).toLowerCase();
        if (label.startsWith ("amp "))
            label = label.fromFirstOccurrenceOf (" ", false, false); // "amp decay" -> "decay"
        const auto sign = rounded > 0 ? juce::String ("+") : juce::String::fromUTF8 ("\xe2\x88\x92"); // + or −
        changes.push_back ({ size, (label + " " + sign + juce::String (std::abs (rounded)) + "%").toStdString() });
    }
    std::sort (changes.begin(), changes.end(), [] (const Change& a, const Change& b) { return a.size > b.size; });
    std::string note;
    for (size_t i = 0; i < std::min<size_t> (2, changes.size()); ++i)
        note += (i > 0 ? " \xc2\xb7 " : "") + changes[i].text;
    return note;
}

} // namespace batida
