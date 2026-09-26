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
    auditioner.prepare (sampleRate);
    auditionEvents.reserve (4096);
    mergedEvents.reserve (8192);
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
    auditioner.reset();
    auditioning = -1;
    ccXy = false;
    mods.reset();
    xyLocked = false;
    switching = 0;
    uiSwitching = 0;
    deferred = {};
}

void Kit::chokeOthers (int voice)
{
    const auto group = current.voices[(size_t) voice].choice (vp::Choke);
    if (group <= 0)
        return;
    for (int o = 0; o < kNumVoices; ++o)
        if (o != voice && current.voices[(size_t) o].choice (vp::Choke) == group && voices[(size_t) o].isActive())
            voices[(size_t) o].choke(); // a quick fade, no click
}

void Kit::auditionPattern (int pattern, bool on)
{
    // Only the latest state counts: releasing one tile and pressing another in
    // the same block plays the second; letting go of one that's no longer the
    // held one changes nothing.
    pattern = std::clamp (pattern, 0, kNumPatterns - 1);
    if (on)
        auditionWanted.store (pattern);
    else
        auditionWanted.compare_exchange_strong (pattern, -1);
}

void Kit::noteOn (int voice, int key, float velocity, int slice)
{
    hitCounts[(size_t) voice].fetch_add (1, std::memory_order_relaxed);
    chokeOthers (voice);
    if ((switching >> voice) & 1u)
    {
        deferred[(size_t) voice] = { true, false, key, slice, velocity };
        return;
    }
    voices[(size_t) voice].noteOn (key, velocity, slice);
}

void Kit::noteOff (int voice, int key)
{
    auto& d = deferred[(size_t) voice];
    if (d.pending && d.key == key)
        d.released = true;
    voices[(size_t) voice].noteOff (key);
}

void Kit::finishSwitches()
{
    if (switching == 0)
        return;
    bool anySolo = false;
    for (const auto& v : current.voices)
        anySolo = anySolo || v.flag (vp::Solo);

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto bit = 1u << v;
        if ((switching & bit) == 0 || voices[(size_t) v].isActive())
            continue;
        switching &= ~bit;
        voices[(size_t) v].setSampleData (slots[(size_t) v].acquire());
        applyVoice (v, anySolo);
        auto& d = deferred[(size_t) v];
        if (d.pending)
        {
            voices[(size_t) v].noteOn (d.key, d.velocity, d.slice);
            if (d.released)
                voices[(size_t) v].noteOff (d.key);
        }
        d = {};
    }
    uiSwitching = switching;
}

void Kit::setParameters (const KitParams& params)
{
    // Sounds about to be replaced: sounding voices fade out on their old settings.
    if (const auto requested = switchRequests.exchange (0); requested != 0)
    {
        for (int v = 0; v < kNumVoices; ++v)
            if (((requested >> v) & 1u) != 0 && voices[(size_t) v].isActive())
            {
                voices[(size_t) v].choke();
                switching |= 1u << v;
            }
        uiSwitching = switching;
    }

    host = params;
    movementData = movement.acquire();

    // The pad (or its automation) moved: it takes back over from the XY CCs.
    const auto hx = params.global[gp::XyX], hy = params.global[gp::XyY];
    if (ccXy && (std::abs (hx - hostX) > 1.0e-4f || std::abs (hy - hostY) > 1.0e-4f))
        ccXy = false;
    hostX = hx;
    hostY = hy;

    const auto mode = (MidiMode) std::clamp ((int) (params.global[gp::MidiMode] + 0.5f), 0, 1);
    const auto keys = std::clamp ((int) (params.global[gp::KeysVoice] + 0.5f), 0, kNumVoices - 1);

    // Held notes would otherwise get their note-offs routed to another voice:
    // all of them when the mode changes, and the old Keys Sound's in Chromatic
    // mode (in Drum map mode Keys Sound routes nothing).
    if (mode != midiMode)
        for (auto& v : voices)
            v.allNotesOff();
    else if (mode == MidiMode::Chromatic && keys != keysVoice)
        voices[(size_t) keysVoice].allNotesOff();

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
        if (((switching >> v) & 1u) == 0) // a switching voice keeps its old sound until it has faded
            applyVoice (v, anySolo);
    }

    if (modulatedOnly && ! globalsModulated)
        return;
    const auto db = current.global[gp::Master];
    masterTarget = db <= -60.0f ? 0.0f : std::pow (10.0f, db / 20.0f);
    globals = current.global;
    applyChainSettings();
}

void Kit::applyVoice (int v, bool anySolo)
{
    const auto& p = current.voices[(size_t) v];
    voices[(size_t) v].setAudible (! p.flag (vp::Mute) && (! anySolo || p.flag (vp::Solo)));
    voices[(size_t) v].setParameters (p);
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
    // A step's XY lock stands in for the pad while it lasts; otherwise the
    // XY CCs do, until the pad moves.
    auto g = globals;
    const auto locked = xyLocked || ccXy;
    const auto x = xyLocked ? lockX : ccX, y = xyLocked ? lockY : ccY;
    if (locked)
    {
        g[gp::XyX] = x;
        g[gp::XyY] = y;
    }
    const auto settings = computeEffectiveChain (g);
    chain.setSettings (settings);
    safetyClip = settings.safetyClip;
    uiXyLocked = locked;
    uiLockX = x;
    uiLockY = y;
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
        case SeqEvent::Type::NoteOn:    noteOn (e.voice, e.key, e.velocity, e.slice); mods.hit (e.voice); break;
        case SeqEvent::Type::NoteOff:   noteOff (e.voice, e.key); break;
        case SeqEvent::Type::XyLock:    xyLocked = true; lockX = e.x; lockY = e.y; applyChainSettings(); break;
        case SeqEvent::Type::XyRelease: xyLocked = false; applyChainSettings(); break;
        case SeqEvent::Type::PatternStart: mods.patternStart(); break;
    }
}

void Kit::handle (const juce::MidiMessage& message)
{
    if (message.isController()
        && (message.getControllerNumber() == kXyCcX || message.getControllerNumber() == kXyCcY))
    {
        if (! ccXy)
        {
            ccX = host.global[gp::XyX];
            ccY = host.global[gp::XyY];
            ccXy = true;
        }
        (message.getControllerNumber() == kXyCcX ? ccX : ccY) = (float) message.getControllerValue() / 127.0f;
        applyChainSettings();
        return;
    }

    if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        for (auto& v : voices)
            message.isAllSoundOff() ? v.reset() : v.allNotesOff();
        return;
    }

    VoiceEvent e;
    if (! routeMidi (message, midiMode, keysVoice, e))
        return;

    if (e.isNoteOn)
    {
        noteOn (e.voice, e.key, e.velocity);
        mods.hit (e.voice);
    }
    else
        noteOff (e.voice, e.key);
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

void Kit::mergeEvents()
{
    // Both lists are sorted; merge them without allocating (capacity is reserved).
    if (auditionEvents.empty())
        return;
    mergedEvents.clear();
    size_t a = 0, b = 0;
    while (a < seqEvents.size() || b < auditionEvents.size())
    {
        if (b >= auditionEvents.size() || (a < seqEvents.size() && seqEvents[a].offset <= auditionEvents[b].offset))
            mergedEvents.push_back (seqEvents[a++]);
        else
            mergedEvents.push_back (auditionEvents[b++]);
    }
    std::swap (seqEvents, mergedEvents);
}

void Kit::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi,
                   const juce::AudioBuffer<float>* sidechain, const Transport& transport)
{
    buffer.clear();
    if (scratch.getNumSamples() == 0) // not prepared yet
        return;

    // A switching voice finishes on its old sample. A sounding voice whose
    // sample has changed fades out first, like a switch, instead of cutting.
    for (int v = 0; v < kNumVoices; ++v)
    {
        if (((switching >> v) & 1u) != 0)
            continue;
        auto& voice = voices[(size_t) v];
        if (voice.isActive() && slots[(size_t) v].peek() != voice.getSampleData())
        {
            voice.choke();
            switching |= 1u << v;
            uiSwitching = switching;
        }
        else
            voice.setSampleData (slots[(size_t) v].acquire());
    }

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

    // A held Vary suggestion plays in place of the real patterns.
    const auto* realBank = patterns.acquire();
    const auto* pv = preview.acquire();
    const PatternBank* bank = realBank;
    if (pv != nullptr && pv->active && realBank != nullptr)
    {
        previewBank = *realBank; // a plain copy into a member: fixed size, no allocation
        previewBank.patterns[(size_t) std::clamp (pv->index, 0, kNumPatterns - 1)] = pv->pattern;
        bank = &previewBank;
    }

    seqEvents.clear();
    sequencer.generate (total, transport, seqSettings, bank, seqEvents);

    // The auditioner: like holding a pattern key, from the next step, when
    // the sequencer isn't running anyway.
    auditionEvents.clear();
    // The real sequencer running takes over: the auditioner lets go.
    const auto wanted = sequencer.isRunning() ? -1 : auditionWanted.load();
    if (wanted != auditioning)
    {
        if (auditioning >= 0)
            auditioner.keyUp (auditioning, 0);
        if (wanted >= 0)
            auditioner.keyDown (wanted, 0.8f, 0);
        auditioning = wanted;
    }
    auto s = seqSettings;
    s.run = RunMode::Keys;
    s.latch = false;
    s.quantise = Quantise::Step;
    auditioner.generate (total, transport, s, bank, auditionEvents);
    mergeEvents();
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
                finishSwitches();
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
