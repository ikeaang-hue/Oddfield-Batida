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

    scratch.setSize (2, std::max (1, maxBlockSize));
    master.setTime (5.0f, sampleRate);
    master.snap (masterTarget);
}

void Kit::reset()
{
    for (auto& v : voices)
        v.reset();
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

void Kit::renderSegment (int start, int numSamples, juce::AudioBuffer<float>& buffer)
{
    // Render through the scratch buffer so any host block size and channel
    // count works without allocating.
    while (numSamples > 0)
    {
        const auto n = std::min (numSamples, scratch.getNumSamples());
        scratch.clear (0, n);
        auto* l = scratch.getWritePointer (0);
        auto* r = scratch.getWritePointer (1);

        for (auto& v : voices)
            v.render (l, r, n);

        for (int i = 0; i < n; ++i)
        {
            const auto g = master.next (masterTarget);
            l[i] *= g;
            r[i] *= g;
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

void Kit::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi)
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
            renderSegment (cursor, pos - cursor, buffer);
            cursor = pos;
        }
        handle (metadata.getMessage());
    }

    if (cursor < total)
        renderSegment (cursor, total - cursor, buffer);
}

} // namespace batida
