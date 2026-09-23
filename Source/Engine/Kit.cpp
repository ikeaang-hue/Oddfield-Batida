#include "Kit.h"

#include "ParamRange.h"

#include <algorithm>
#include <cmath>

namespace batida
{

void Kit::prepare (double sampleRate, int maxBlockSize)
{
    jassert (sampleRate > 0.0);
    sampleRateHz = sampleRate;
    for (auto& v : voices)
        v.prepare (sampleRate);

    scratch.setSize (4, std::max (1, maxBlockSize));
    sequencer.prepare (sampleRate);
    mods.prepare (sampleRate);
    seqEvents.reserve (4096);
    chain.prepare (sampleRate, std::max (1, maxBlockSize));
    master.setTime (5.0f, sampleRate);
    master.snap (masterTarget);
}

void Kit::reset()
{
    for (auto& v : voices)
        v.reset();
    chain.reset();
    sequencer.reset();
    mods.reset();
    xyLocked = false;
}

void Kit::setParameters (const KitParams& params)
{
    host = params;
    movementData = movement.acquire();

    const auto mode = (MidiMode) std::clamp ((int) (params.global[gp::MidiMode] + 0.5f), 0, 1);
    const auto keys = std::clamp ((int) (params.global[gp::KeysVoice] + 0.5f), 0, kNumVoices - 1);

    // Held notes would otherwise get their note-offs routed to another voice.
    if (mode != midiMode || keys != keysVoice)
        for (auto& v : voices)
            v.allNotesOff();

    midiMode = mode;
    keysVoice = keys;

    const auto& g = params.global;
    seqSettings.run = (RunMode) std::clamp ((int) (g[gp::SeqRun] + 0.5f), 0, 1);
    seqSettings.pattern = std::clamp ((int) (g[gp::SeqPattern] + 0.5f), 0, kNumPatterns - 1);
    seqSettings.play = g[gp::SeqPlay] > 0.5f;
    seqSettings.tempo = g[gp::SeqTempo];
    seqSettings.swing = g[gp::SeqSwing];
    seqSettings.quantise = (Quantise) std::clamp ((int) (g[gp::SeqQuantise] + 0.5f), 0, 2);
    seqSettings.latch = g[gp::SeqLatch] > 0.5f;
    seqSettings.sync = g[gp::SeqSync] > 0.5f;

    // With the modulators' latest values, so a block never starts unmodulated.
    computeParams();
    applyParams (false);
}

void Kit::computeParams()
{
    current = host;
    voiceModulated.fill (false);
    globalsModulated = anyTargets = false;
    if (movementData == nullptr)
        return;

    auto offset = [&] (float& value, const juce::NormalisableRange<float>& range, float amount)
    {
        value = range.convertFrom0to1 (std::clamp (range.convertTo0to1 (value) + amount, 0.0f, 1.0f));
    };

    // 1. The scene morph (itself a possible modulation target).
    auto& g = current.global;
    for (int m = 0; m < kNumMods; ++m)
        for (const auto& t : movementData->mods[(size_t) m].targets)
            if (t.active && t.global && t.param == gp::SceneMorph)
                offset (g[gp::SceneMorph], globalRanges()[(size_t) gp::SceneMorph], t.depth * mods.value (m));

    if (g[gp::SceneMorphOn] > 0.5f)
    {
        const auto& a = movementData->scenes[(size_t) std::clamp ((int) (g[gp::SceneA] + 0.5f), 0, kNumScenes - 1)];
        const auto& b = movementData->scenes[(size_t) std::clamp ((int) (g[gp::SceneB] + 0.5f), 0, kNumScenes - 1)];
        if (a.stored && b.stored)
        {
            const auto x = std::clamp (g[gp::SceneMorph], 0.0f, 1.0f);
            for (const auto p : sceneParams())
                g[(size_t) p] = isContinuous (globalParamSpecs()[(size_t) p])
                                    ? a.values[(size_t) p] + (b.values[(size_t) p] - a.values[(size_t) p]) * x
                                    : (x < 0.5f ? a.values[(size_t) p] : b.values[(size_t) p]);
            globalsModulated = true;
        }
    }

    // 2. Everything else, on top of the knobs (and the morphed scene).
    for (int m = 0; m < kNumMods; ++m)
    {
        const auto v = mods.value (m);
        for (const auto& t : movementData->mods[(size_t) m].targets)
        {
            if (! t.active || (t.global && t.param == gp::SceneMorph))
                continue;
            anyTargets = true;
            if (t.global)
            {
                offset (g[(size_t) t.param], globalRanges()[(size_t) t.param], t.depth * v);
                globalsModulated = true;
            }
            else
            {
                offset (current.voices[(size_t) t.voice][t.param], voiceRanges()[(size_t) t.param], t.depth * v);
                voiceModulated[(size_t) t.voice] = true;
            }
        }
        anyTargets = anyTargets || movementData->mods[(size_t) m].hasTargets();
    }
}

void Kit::applyParams (bool modulatedOnly)
{
    // Solo wins: while any voice is soloed, only soloed voices sound.
    bool anySolo = false;
    for (const auto& v : current.voices)
        anySolo = anySolo || v.flag (vp::Solo);

    for (int v = 0; v < kNumVoices; ++v)
    {
        if (modulatedOnly && ! voiceModulated[(size_t) v])
            continue;
        const auto& p = current.voices[(size_t) v];
        voices[(size_t) v].setAudible (! p.flag (vp::Mute) && (! anySolo || p.flag (vp::Solo)));
        voices[(size_t) v].setParameters (p);
    }

    if (modulatedOnly && ! globalsModulated)
        return;
    const auto db = current.global[gp::Master];
    masterTarget = db <= -60.0f ? 0.0f : std::pow (10.0f, db / 20.0f);
    globals = current.global;
    applyChainSettings();
}

void Kit::tick (int offset)
{
    if (movementData == nullptr)
        return;
    mods.advance (kControlSamples, clockPpq + offset * clockPpqPerSample, clockPpqPerSample, host.global, *movementData);
    if (anyTargets || host.global[gp::SceneMorphOn] > 0.5f)
    {
        computeParams();
        applyParams (true);
    }
}

void Kit::applyChainSettings()
{
    // A step's XY lock stands in for the pad while it lasts.
    auto g = globals;
    if (xyLocked)
    {
        g[gp::XyX] = lockX;
        g[gp::XyY] = lockY;
    }
    const auto settings = computeEffectiveChain (g);
    chain.setSettings (settings);
    safetyClip = settings.safetyClip;
    uiXyLocked = xyLocked;
    uiLockX = lockX;
    uiLockY = lockY;
}

bool Kit::isPatternKey (const juce::MidiMessage& m) const
{
    return (m.isNoteOn() || m.isNoteOff()) && m.getNoteNumber() >= kPatternKeyFirst && m.getNoteNumber() <= kPatternKeyLast
        && (midiMode == MidiMode::DrumMap || m.getChannel() == kDrumChannel);
}

void Kit::handle (const SeqEvent& e)
{
    switch (e.type)
    {
        case SeqEvent::Type::NoteOn:    voices[(size_t) e.voice].noteOn (e.key, e.velocity, e.slice); mods.hit (e.voice); break;
        case SeqEvent::Type::NoteOff:   voices[(size_t) e.voice].noteOff (e.key); break;
        case SeqEvent::Type::XyLock:    xyLocked = true; lockX = e.x; lockY = e.y; applyChainSettings(); break;
        case SeqEvent::Type::XyRelease: xyLocked = false; applyChainSettings(); break;
        case SeqEvent::Type::PatternStart: mods.patternStart(); break;
    }
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
    {
        voice.noteOn (e.key, e.velocity);
        mods.hit (e.voice);
    }
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
                   const juce::AudioBuffer<float>* sidechain, const Transport& transport)
{
    buffer.clear();
    if (scratch.getNumSamples() == 0) // not prepared yet
        return;

    for (int v = 0; v < kNumVoices; ++v)
        voices[(size_t) v].setSampleData (slots[(size_t) v].acquire());

    const auto total = buffer.getNumSamples();

    // Pattern keys go to the sequencer, which turns them into this block's hits.
    for (const auto metadata : midi)
    {
        const auto m = metadata.getMessage();
        if (! isPatternKey (m))
            continue;
        const auto pattern = m.getNoteNumber() - kPatternKeyFirst;
        if (m.isNoteOn())
            sequencer.keyDown (pattern, m.getFloatVelocity(), metadata.samplePosition);
        else
            sequencer.keyUp (pattern, metadata.samplePosition);
    }

    seqEvents.clear();
    sequencer.generate (total, transport, seqSettings, patterns.acquire(), seqEvents);
    uiFollowingHost = transport.hostPlaying && seqSettings.sync;
    uiHostBpm = transport.bpm;

    // The modulators' clock: the host's position when following it, else ours.
    if (uiFollowingHost)
    {
        clockPpq = transport.ppq;
        clockPpqPerSample = std::max (1.0, transport.bpm) / 60.0 / sampleRateHz;
    }
    else
    {
        clockPpq = internalClock;
        clockPpqPerSample = std::max (1.0, seqSettings.tempo) / 60.0 / sampleRateHz;
        internalClock += total * clockPpqPerSample;
    }

    // Merge live MIDI and sequencer events in time order.
    int cursor = 0;
    size_t next = 0;
    auto renderTo = [&] (int pos)
    {
        // In control steps: the modulators move every kControlSamples.
        pos = std::clamp (pos, 0, total);
        while (cursor < pos)
        {
            if (controlCountdown <= 0)
            {
                tick (cursor);
                controlCountdown = kControlSamples;
            }
            const auto n = std::min (pos - cursor, controlCountdown);
            renderSegment (cursor, n, buffer, sidechain);
            cursor += n;
            controlCountdown -= n;
        }
    };

    for (const auto metadata : midi)
    {
        while (next < seqEvents.size() && seqEvents[next].offset <= metadata.samplePosition)
        {
            renderTo (seqEvents[next].offset);
            handle (seqEvents[next++]);
        }
        const auto m = metadata.getMessage();
        if (isPatternKey (m))
            continue;
        renderTo (metadata.samplePosition);
        handle (m);
    }
    while (next < seqEvents.size())
    {
        renderTo (seqEvents[next].offset);
        handle (seqEvents[next++]);
    }

    renderTo (total);
}

} // namespace batida
