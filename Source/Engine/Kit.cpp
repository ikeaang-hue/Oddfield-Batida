#include "Kit.h"

#include <algorithm>
#include <cmath>

namespace batida
{

void Kit::prepare (double sampleRate, int maxBlockSize)
{
    jassert (sampleRate > 0.0);
    for (auto& v : voices)
        v.prepare (sampleRate);

    scratch.setSize (4, std::max (1, maxBlockSize));
    chain.prepare (sampleRate, std::max (1, maxBlockSize));
    master.setTime (5.0f, sampleRate);
    master.snap (masterTarget);
}

void Kit::reset()
{
    for (auto& v : voices)
        v.reset();
    chain.reset();
}

void Kit::setParameters (const KitParams& params)
{
    for (int v = 0; v < kNumVoices; ++v)
        voices[(size_t) v].setParameters (params.voices[(size_t) v]);

    const auto mode = (MidiMode) std::clamp ((int) (params.global[gp::MidiMode] + 0.5f), 0, 1);
    const auto keys = std::clamp ((int) (params.global[gp::KeysVoice] + 0.5f), 0, kNumVoices - 1);

    // Held notes would otherwise get their note-offs routed to another voice.
    if (mode != midiMode || keys != keysVoice)
        for (auto& v : voices)
            v.allNotesOff();

    midiMode = mode;
    keysVoice = keys;
    const auto db = params.global[gp::Master];
    masterTarget = db <= -60.0f ? 0.0f : std::pow (10.0f, db / 20.0f);

    const auto settings = computeEffectiveChain (params.global);
    chain.setSettings (settings);
    safetyClip = settings.safetyClip;
}

void Kit::handle (const juce::MidiMessage& message)
{
    if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        for (auto& v : voices)
            message.isAllSoundOff() ? v.reset() : v.allNotesOff();
        return;
    }

    VoiceEvent e;
    if (! routeMidi (message, midiMode, keysVoice, e))
        return;

    auto& voice = voices[(size_t) e.voice];
    if (e.isNoteOn)
        voice.noteOn (e.key, e.velocity);
    else
        voice.noteOff (e.key);
}

namespace
{
// Soft ceiling: linear up to -1 dBFS, then bends smoothly towards 0 dBFS.
inline float softClip (float x)
{
    constexpr float t = 0.891f;
    const auto a = std::abs (x);
    if (a <= t)
        return x;
    return std::copysign (t + (1.0f - t) * std::tanh ((a - t) / (1.0f - t)), x);
}
} // namespace

void Kit::renderSegment (int start, int numSamples, juce::AudioBuffer<float>& buffer,
                         const juce::AudioBuffer<float>* sidechain)
{
    // Render through the scratch buses so any host block size and channel
    // count works without allocating.
    while (numSamples > 0)
    {
        const auto n = std::min (numSamples, scratch.getNumSamples());
        scratch.clear (0, n);
        auto* dl = scratch.getWritePointer (0);
        auto* dr = scratch.getWritePointer (1);
        auto* wl = scratch.getWritePointer (2);
        auto* wr = scratch.getWritePointer (3);

        for (auto& v : voices)
            v.render (dl, dr, wl, wr, n);

        const float* scL = nullptr;
        const float* scR = nullptr;
        if (sidechain != nullptr && sidechain->getNumChannels() > 0)
        {
            scL = sidechain->getReadPointer (0, start);
            scR = sidechain->getReadPointer (std::min (1, sidechain->getNumChannels() - 1), start);
        }

        chain.process (dl, dr, wl, wr, scL, scR, n); // result in dl/dr

        for (int i = 0; i < n; ++i)
        {
            const auto g = master.next (masterTarget);
            dl[i] *= g;
            dr[i] *= g;
            if (safetyClip)
            {
                dl[i] = softClip (dl[i]);
                dr[i] = softClip (dr[i]);
            }
        }

        if (buffer.getNumChannels() >= 2)
        {
            buffer.copyFrom (0, start, scratch, 0, 0, n);
            buffer.copyFrom (1, start, scratch, 1, 0, n);
        }
        else if (buffer.getNumChannels() == 1)
        {
            buffer.addFrom (0, start, scratch, 0, 0, n, 0.5f);
            buffer.addFrom (0, start, scratch, 1, 0, n, 0.5f);
        }

        start += n;
        numSamples -= n;
    }
}

void Kit::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi,
                   const juce::AudioBuffer<float>* sidechain)
{
    buffer.clear();
    if (scratch.getNumSamples() == 0) // not prepared yet
        return;

    for (int v = 0; v < kNumVoices; ++v)
        voices[(size_t) v].setSampleData (slots[(size_t) v].acquire());

    const auto total = buffer.getNumSamples();
    int cursor = 0;

    for (const auto metadata : midi)
    {
        const auto pos = std::clamp (metadata.samplePosition, 0, total);
        if (pos > cursor)
        {
            renderSegment (cursor, pos - cursor, buffer, sidechain);
            cursor = pos;
        }
        handle (metadata.getMessage());
    }

    if (cursor < total)
        renderSegment (cursor, total - cursor, buffer, sidechain);
}

} // namespace batida
