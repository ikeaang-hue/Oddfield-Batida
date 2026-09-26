#include "BreakKit.h"

#include "SampleSource.h"

#include <algorithm>
#include <cmath>

namespace batida
{

int slotFor (HitKind kind)
{
    switch (kind)
    {
        case HitKind::Kick:      return 0;
        case HitKind::Perc:      return 1;
        case HitKind::Snare:     return 2;
        case HitKind::ClosedHat: return 6;
        case HitKind::OpenHat:   return 7;
    }
    return 1;
}

HitFeatures analyseHit (const SampleData& data, int from, int to)
{
    HitFeatures f;
    // Up to 300 ms of the hit, mixed to mono: how much of its energy is low
    // (below ~120 Hz, two poles so a snare's body doesn't count), bright
    // (above ~2 kHz) and high (above ~6 kHz), and how long it rings.
    const auto rate = data.sampleRate;
    to = std::min ({ to, data.numFrames(), from + (int) (0.3 * rate) });
    if (to - from < 16)
        return f;

    const auto aLow = 1.0f - (float) std::exp (-6.2831853 * 120.0 / rate);
    const auto aHigh = 1.0f - (float) std::exp (-6.2831853 * 6000.0 / rate);
    const auto aMid = 1.0f - (float) std::exp (-6.2831853 * 2000.0 / rate);
    float low1 = 0.0f, low = 0.0f, lp6k = 0.0f, lp2k = 0.0f;
    double eAll = 0.0, eLow = 0.0, eHigh = 0.0, eBright = 0.0;
    const auto window = std::max (1, (int) (0.005 * rate));
    std::vector<float> envelope;
    float windowPeak = 0.0f;
    for (int i = from; i < to; ++i)
    {
        float x = 0.0f;
        for (int ch = 0; ch < data.numChannels(); ++ch)
            x += data.audio.getSample (ch, i);
        x /= (float) std::max (1, data.numChannels());
        low1 += aLow * (x - low1);
        low += aLow * (low1 - low);
        lp6k += aHigh * (x - lp6k);
        lp2k += aMid * (x - lp2k);
        const auto high = x - lp6k, bright = x - lp2k;
        eAll += (double) x * x;
        eLow += (double) low * low;
        eHigh += (double) high * high;
        eBright += (double) bright * bright;
        windowPeak = std::max (windowPeak, std::abs (x));
        if ((i - from) % window == window - 1)
        {
            envelope.push_back (windowPeak);
            windowPeak = 0.0f;
        }
    }
    if (eAll < 1.0e-9)
        return f;

    const auto lowRatio = (float) (eLow / eAll), highRatio = (float) (eHigh / eAll), brightRatio = (float) (eBright / eAll);
    float decayMs = 0.0f;
    if (! envelope.empty())
    {
        const auto peakAt = (size_t) (std::max_element (envelope.begin(), envelope.end()) - envelope.begin());
        const auto peak = envelope[peakAt];
        auto end = envelope.size();
        for (auto k = peakAt; k < envelope.size(); ++k)
            if (envelope[k] < 0.1f * peak) // -20 dB
            {
                end = k;
                break;
            }
        decayMs = (float) ((double) (end - peakAt) * (double) window * 1000.0 / rate);
    }

    // Tonal (a tom, a cowbell) or noisy (a snare, a clap): how periodic 30 ms
    // of the hit is, from its loudest point.
    const auto peakFrame = from + (int) ((envelope.empty() ? 0 : (std::max_element (envelope.begin(), envelope.end()) - envelope.begin())) * window);
    const auto n = std::min ((int) (0.03 * rate), to - peakFrame);
    float periodic = 0.0f;
    if (n > 64)
    {
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            float v = 0.0f;
            for (int ch = 0; ch < data.numChannels(); ++ch)
                v += data.audio.getSample (ch, peakFrame + i);
            x[(size_t) i] = v;
        }
        const auto minLag = std::max (2, (int) (rate / 1500.0)), maxLag = std::min (n / 2, (int) (rate / 60.0));
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double xy = 0.0, xx = 0.0, yy = 0.0;
            for (int i = 0; i + lag < n; ++i)
            {
                xy += (double) x[(size_t) i] * x[(size_t) (i + lag)];
                xx += (double) x[(size_t) i] * x[(size_t) i];
                yy += (double) x[(size_t) (i + lag)] * x[(size_t) (i + lag)];
            }
            if (xx > 0.0 && yy > 0.0)
                periodic = std::max (periodic, (float) (xy / std::sqrt (xx * yy)));
        }
    }
    f.low = lowRatio;
    f.bright = brightRatio;
    f.high = highRatio;
    f.decayMs = decayMs;
    f.periodic = periodic;
    f.silent = false;
    return f;
}

HitKind classifyHit (const HitFeatures& f)
{
    if (f.silent)
        return HitKind::Perc;
    if ((f.low > 0.45f && f.bright < 0.15f) || f.low > 0.7f)
        return HitKind::Kick;
    if (f.high > 0.3f && f.low < 0.1f)
        return f.decayMs > 180.0f ? HitKind::OpenHat : HitKind::ClosedHat;
    if (f.periodic < 0.6f || f.high > 0.1f) // noise: a snare or clap
        return HitKind::Snare;
    return HitKind::Perc;
}

HitKind classifyHit (const SampleData& data, int from, int to)
{
    return classifyHit (analyseHit (data, from, to));
}

std::optional<BreakPlan> planBreak (const SampleData& data)
{
    const auto frames = data.numFrames();
    if (frames < 256 || data.sampleRate <= 0.0)
        return std::nullopt;

    BreakPlan plan;

    // Half a bar, 1, 2 or 4 bars: whichever puts the tempo nearest 125 bpm
    // (within 70–190 preferred), never outside the tempo Batida can play.
    const auto seconds = frames / data.sampleRate;
    double best = 1.0e9;
    for (const auto steps : { 8, 16, 32, 64 })
    {
        const auto bpm = 60.0 * (steps / 4.0) / seconds;
        if (bpm < kBreakMinBpm || bpm > kBreakMaxBpm)
            continue;
        const auto outside = bpm < 70.0 ? 70.0 / bpm : (bpm > 190.0 ? bpm / 190.0 : 1.0);
        const auto score = std::abs (std::log2 (bpm / 125.0)) + 10.0 * (outside - 1.0);
        if (score < best)
        {
            best = score;
            plan.steps = steps;
            plan.bpm = bpm;
        }
    }
    if (best > 1.0e8)
        return std::nullopt; // too short or too long to be a loop at a playable tempo
    plan.bars = plan.steps / 16.0;
    const auto framesPerStep = (double) frames / plan.steps;

    // The strongest 31 onsets become slices (the most a slot can hold); the
    // sensitivity is set so the slot finds exactly those.
    std::vector<std::pair<int, float>> candidates;
    for (const auto& o : data.onsets)
        if (o.first > 64 && o.first < frames - 64)
            candidates.push_back (o);
    std::vector<float> strengths;
    for (const auto& c : candidates)
        strengths.push_back (c.second);
    std::sort (strengths.begin(), strengths.end(), std::greater<float>());
    float threshold = 0.0f;
    if (strengths.size() > 31)
        threshold = 0.5f * (strengths[30] + strengths[31]);
    plan.sensitivity = std::clamp (1.0f - threshold, 0.0f, 1.0f);

    SliceSpec spec;
    spec.mode = SliceMode::Transients;
    spec.sensitivity = plan.sensitivity;
    const auto count = sliceCount (data, spec, 0.0, (double) frames);

    // Loudest point of the loop, to tell a real first hit from silence.
    float loudest = 0.0f;
    for (int ch = 0; ch < data.numChannels(); ++ch)
        loudest = std::max (loudest, data.audio.getMagnitude (ch, 0, frames));
    if (loudest <= 0.0f)
        return std::nullopt;

    std::array<std::array<bool, kMaxSteps>, kNumTracks> taken {};
    for (int k = 0; k < count; ++k)
    {
        const auto [from, to] = sliceRegion (data, spec, 0.0, (double) frames, k);
        const auto start = (int) from, end = (int) to;
        if (end - start < (int) (0.02 * data.sampleRate))
            continue; // a sliver before the first onset
        if (k == 0)
        {
            // Slice 1 runs from the file's start: only a hit if something sounds there.
            float head = 0.0f;
            const auto n = std::min (end - start, (int) (0.03 * data.sampleRate));
            for (int ch = 0; ch < data.numChannels(); ++ch)
                head = std::max (head, data.audio.getMagnitude (ch, start, n));
            if (head < 0.2f * loudest)
                continue;
        }
        const auto step = (int) std::lround (start / framesPerStep);
        if (step >= plan.steps)
            continue; // the next loop's downbeat

        BreakHit hit;
        hit.frame = start;
        hit.slice = k;
        hit.step = step;
        hit.kind = classifyHit (data, start, end);
        const auto slot = slotFor (hit.kind);
        if (taken[(size_t) slot][(size_t) step])
            continue; // one hit per step on a slot (sounds are monophonic)
        taken[(size_t) slot][(size_t) step] = true;
        plan.hits.push_back (hit);
        plan.used[(size_t) slot] = true;
    }
    if (plan.hits.empty())
        return std::nullopt;

    // The pattern: every hit on its step, playing its own slice.
    auto& pat = plan.pattern;
    pat.length = plan.steps;
    for (auto& tr : pat.tracks)
        tr.length = plan.steps;
    for (const auto& h : plan.hits)
    {
        auto& st = pat.tracks[(size_t) slotFor (h.kind)].steps[(size_t) h.step];
        st.gate = true;
        st.velocity = 127; // the loop's own dynamics are in the slices
        st.slice = (uint8_t) h.slice;
    }

    // The slots: the loop, sliced at the same onsets, each slice playing to
    // its end with a short fade so cut-offs don't click.
    static const char* names[] = { "Break Kick", "Break Perc", "Break Snare", nullptr, nullptr, nullptr, "Break Hat", "Break Open Hat" };
    for (int v = 0; v < kNumVoices; ++v)
    {
        if (! plan.used[(size_t) v])
            continue;
        auto& p = plan.voices[(size_t) v];
        for (int k = 0; k < kNumVoiceParams; ++k)
            p[k] = voiceParamSpecs()[(size_t) k].def;
        p[vp::SrcMode] = (float) SourceMode::Sample;
        p[vp::PlayMode] = (float) PlayMode::OneShot;
        p[vp::AmpA] = 0.0f;
        p[vp::AmpD] = 8000.0f;
        p[vp::AmpS] = 1.0f;
        p[vp::AmpR] = 8000.0f;
        p[vp::SmpFadeOut] = 4.0f;
        p[vp::SmpSliceMode] = (float) SliceMode::Transients;
        p[vp::SmpSliceSens] = plan.sensitivity;
        p[vp::Choke] = (v == 6 || v == 7) ? 1.0f : 0.0f;
        plan.names[(size_t) v] = names[v];
    }
    return plan;
}

} // namespace batida
