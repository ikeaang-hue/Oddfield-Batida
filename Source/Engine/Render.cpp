#include "Render.h"

#include <cmath>

namespace batida
{

int patternPassSamples (const Pattern& pattern, double bpm, double sampleRate)
{
    const auto beats = pattern.length * Sequencer::kStep;
    return (int) std::lround (beats * 60.0 / std::max (1.0, bpm) * sampleRate);
}

juce::AudioBuffer<float> renderKit (const RenderRequest& req)
{
    constexpr int block = 512;
    auto kit = std::make_unique<Kit>();
    kit->prepare (req.sampleRate, block);
    for (int v = 0; v < kNumVoices; ++v)
        if (req.samples[(size_t) v] != nullptr)
            kit->sampleSlot (v).setShared (req.samples[(size_t) v]);
    kit->movementStore().replace (req.movement);
    kit->patternStore().replace (req.bank);

    auto params = req.params;
    auto& g = params.global;
    const auto patternMode = req.mode == RenderRequest::Mode::Pattern;
    g[gp::SeqRun] = patternMode ? 1.0f : 0.0f;  // Transport: runs on Play
    g[gp::SeqPlay] = patternMode ? 1.0f : 0.0f;
    g[gp::SeqSync] = 0.0f;                      // our own clock, at the given tempo
    g[gp::SeqTempo] = (float) req.bpm;
    g[gp::SeqPattern] = (float) req.pattern;

    const auto latency = kit->getLatencySamples();
    const auto pass = patternMode ? patternPassSamples (req.bank.patterns[(size_t) std::clamp (req.pattern, 0, kNumPatterns - 1)], req.bpm, req.sampleRate) : 0;
    const auto maxSound = (int) (6.0 * req.sampleRate);
    const auto total = patternMode ? 2 * pass + latency + block : maxSound + latency + block;

    juce::AudioBuffer<float> all (2, total);
    all.clear();
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer none;

    // One block first (sequencer stopped), so the voices pick up their samples
    // before the first hit; the pass then starts exactly at sample 0.
    auto pickup = params;
    pickup.global[gp::SeqPlay] = 0.0f;
    kit->setParameters (pickup);
    kit->process (buffer, none);

    const auto gate = (PlayMode) params.voices[(size_t) req.voice].choice (vp::PlayMode) == PlayMode::Gate;
    const auto releaseAt = (int) (0.5 * req.sampleRate);
    if (! patternMode)
        kit->noteOn (req.voice, Voice::kBaseKey, 1.0f);

    int done = 0, quietFor = 0;
    const auto quietNeeded = (int) (0.2 * req.sampleRate);
    while (done < total)
    {
        const auto n = std::min (block, total - done);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
        kit->setParameters (params);
        if (! patternMode && gate && done <= releaseAt && done + n > releaseAt)
            kit->noteOff (req.voice, Voice::kBaseKey);
        kit->process (view, none);
        for (int ch = 0; ch < 2; ++ch)
            all.copyFrom (ch, done, view, ch, 0, n);
        done += n;

        if (! patternMode && done > latency + (int) (0.05 * req.sampleRate) && (! gate || done > releaseAt))
        {
            const auto peak = std::max (view.getMagnitude (0, 0, n), view.getMagnitude (1, 0, n));
            quietFor = peak < 1.0e-4f ? quietFor + n : 0; // -80 dB
            if (quietFor >= quietNeeded)
                break;
        }
    }

    // Skip the chain's latency (and, for a pattern, the first pass).
    const auto from = latency + pass;
    const auto length = patternMode ? pass : std::max (1, done - latency - quietFor + (int) (0.02 * req.sampleRate));
    juce::AudioBuffer<float> out (2, std::max (1, std::min (length, done - from)));
    for (int ch = 0; ch < 2; ++ch)
        out.copyFrom (ch, 0, all, ch, from, out.getNumSamples());
    return out;
}

} // namespace batida
