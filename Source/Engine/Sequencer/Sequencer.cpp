#include "Sequencer.h"

#include <algorithm>
#include <cmath>

namespace batida
{

namespace
{
// Repeatable "random" roll for a step: the same step of the same pattern
// always rolls the same, so bounces are identical. Ratchets share one roll.
int roll (int pattern, long long step, int track)
{
    auto z = (uint64_t) (pattern + 1) * 0x9E3779B97F4A7C15ull ^ (uint64_t) step * 0xBF58476D1CE4E5B9ull
           ^ (uint64_t) (track + 1) * 0x94D049BB133111EBull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (int) (z % 100);
}
} // namespace

void Sequencer::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    offs.reserve (256);
    reset();
}

void Sequencer::reset()
{
    run = pending = {};
    numHeld = numCommands = 0;
    offs.clear();
    lastHostPlaying = lastLatch = false;
    lastParamPattern = -1;
    lastEnd = -1.0;
    internalPpq = 0.0;
    xyActive = false;
    uiRunning = false;
    uiPatternStep = -1;
    for (auto& s : uiTrackStep)
        s = -1;
}

void Sequencer::keyDown (int pattern, float velocity, int offset)
{
    if (numCommands < (int) commands.size())
        commands[(size_t) numCommands++] = { true, pattern, offset, velocity };
}

void Sequencer::keyUp (int pattern, int offset)
{
    if (numCommands < (int) commands.size())
        commands[(size_t) numCommands++] = { false, pattern, offset, 0.0f };
}

long long Sequencer::sampleOf (double t) const
{
    return std::llround ((t - blockStart) / ppqPerSample);
}

int Sequencer::offsetOf (double t) const
{
    // Decisions are made on whole samples, so a hit on a block boundary lands
    // in exactly one block, on its exact sample.
    return (int) std::clamp<long long> (sampleOf (t), 0, std::max (0, blockSamples - 1));
}

void Sequencer::push (SeqEvent e)
{
    out->push_back (e);
}

double Sequencer::quantised (double t) const
{
    if (! synced)
        return t; // internal clock: start right away
    const auto grid = settings.quantise == Quantise::Step ? kStep
                    : settings.quantise == Quantise::Beat ? 1.0 : beatsPerBar;
    return std::ceil (t / grid - 1.0e-9) * grid;
}

void Sequencer::stopAt (double t)
{
    // Release everything now.
    for (const auto& o : offs)
    {
        SeqEvent e;
        e.type = SeqEvent::Type::NoteOff;
        e.offset = offsetOf (t);
        e.voice = o.voice;
        e.key = o.key;
        push (e);
    }
    offs.clear();

    if (xyActive)
    {
        SeqEvent e;
        e.type = SeqEvent::Type::XyRelease;
        e.offset = offsetOf (t);
        push (e);
        xyActive = false;
    }
    run.active = pending.active = false;
}

void Sequencer::startOrSwitch (int pattern, float velocity, double t)
{
    Run r;
    r.active = true;
    r.pattern = pattern;
    r.velocityScale = settings.run == RunMode::Keys ? std::clamp (velocity / 0.8f, 0.1f, 1.27f) : 1.0f;

    if (settings.run == RunMode::Transport && run.active)
    {
        // Keep the song-aligned grid; only the pattern changes.
        r.origin = run.origin;
        r.parityBase = run.parityBase;
        pending = r;
        pendingSwitchAt = quantised (t);
        return;
    }

    r.origin = quantised (t);
    r.parityBase = synced ? std::llround (r.origin / kStep) : 0;

    if (! run.active || r.origin <= t)
    {
        if (run.active)
            stopAt (t);
        run = r;
    }
    else
    {
        pending = r;
        pendingSwitchAt = r.origin;
    }
}

void Sequencer::flushOffsets (double to)
{
    for (size_t i = 0; i < offs.size();)
    {
        if (sampleOf (offs[i].time) < sampleOf (to))
        {
            SeqEvent e;
            e.type = SeqEvent::Type::NoteOff;
            e.offset = offsetOf (offs[i].time);
            e.voice = offs[i].voice;
            e.key = offs[i].key;
            push (e);
            offs[i] = offs.back();
            offs.pop_back();
        }
        else
            ++i;
    }
}

void Sequencer::emitRun (const Run& r, double from, double to)
{
    if (! r.active || bank == nullptr || to <= r.origin)
        return;

    const auto& pat = bank->patterns[(size_t) std::clamp (r.pattern, 0, kNumPatterns - 1)];
    const auto lo = std::max (from, r.origin);
    const auto swingDelay = (double) (std::clamp (settings.swing, 0.5f, 0.75f) - 0.5f) * 0.5;
    const auto first = std::max (0LL, (long long) std::floor ((lo - r.origin) / kStep) - 1);
    const auto last = (long long) std::floor ((to - r.origin) / kStep) + 1;
    const auto loSample = sampleOf (lo), toSample = sampleOf (to);
    auto inside = [&] (double t)
    {
        const auto si = sampleOf (t);
        return t >= r.origin - 1.0e-9 && si >= loSample && si < toSample;
    };

    for (auto g = first; g <= last; ++g)
    {
        const auto swung = ((r.parityBase + g) & 1) != 0;
        const auto stepTime = r.origin + (double) g * kStep + (swung ? swingDelay : 0.0);
        const auto patStep = (int) (g % pat.length);

        // XY lane: a lock moves the pad for this step only.
        if (inside (stepTime))
        {
            const auto& lock = pat.xy[(size_t) patStep];
            if (lock.active)
            {
                SeqEvent e;
                e.type = SeqEvent::Type::XyLock;
                e.offset = offsetOf (stepTime);
                e.x = lock.x;
                e.y = lock.y;
                push (e);
                xyActive = true;
            }
            else if (xyActive)
            {
                SeqEvent e;
                e.type = SeqEvent::Type::XyRelease;
                e.offset = offsetOf (stepTime);
                push (e);
                xyActive = false;
            }
        }

        for (int t = 0; t < kNumTracks; ++t)
        {
            const auto& tr = pat.tracks[(size_t) t];
            const auto len = std::clamp (std::min (tr.length, pat.length), 1, kMaxSteps);
            const auto& step = tr.steps[(size_t) (g % len)];
            if (! step.gate)
                continue;
            if (step.probability < 100 && roll (r.pattern, g, t) >= step.probability)
                continue;

            const auto ratchets = std::clamp ((int) step.ratchet, 1, kMaxRatchet);
            const auto sub = kStep / ratchets;
            for (int k = 0; k < ratchets; ++k)
            {
                const auto hit = stepTime + k * sub;
                if (! inside (hit))
                    continue;

                const auto key = 60 + step.pitch;

                // Mono voices: close this track's previous note first.
                for (size_t i = 0; i < offs.size();)
                {
                    if (offs[i].voice == t)
                    {
                        SeqEvent off;
                        off.type = SeqEvent::Type::NoteOff;
                        off.offset = offsetOf (std::min (offs[i].time, hit));
                        off.voice = t;
                        off.key = offs[i].key;
                        push (off);
                        offs[i] = offs.back();
                        offs.pop_back();
                    }
                    else
                        ++i;
                }

                SeqEvent on;
                on.type = SeqEvent::Type::NoteOn;
                on.offset = offsetOf (hit);
                on.voice = t;
                on.key = key;
                on.slice = step.slice;
                on.velocity = std::min (1.0f, step.velocity / 127.0f * r.velocityScale);
                push (on);

                offs.push_back ({ hit + std::max (0.002, tr.noteLength * sub), t, key });
            }
        }
    }

    // Playheads for the UI: the step under the end of this interval.
    const auto now = (long long) std::floor ((to - r.origin) / kStep);
    if (now >= 0)
    {
        uiPatternStep = (int) (now % pat.length);
        for (int t = 0; t < kNumTracks; ++t)
        {
            const auto len = std::clamp (std::min (pat.tracks[(size_t) t].length, pat.length), 1, kMaxSteps);
            uiTrackStep[(size_t) t] = (int) (now % len);
        }
    }
}

void Sequencer::emitInterval (double from, double to)
{
    if (to <= from)
        return;

    // A quantised start or switch that falls inside this interval.
    if (pending.active && pendingSwitchAt < to)
    {
        const auto at = std::max (from, pendingSwitchAt);
        emitRun (run, from, at);
        flushOffsets (at);
        const auto next = pending;
        pending.active = false;
        if (settings.run == RunMode::Keys && run.active)
            stopAt (at); // Keys: the new pattern starts from its first step
        run = next;
        from = at;
    }

    // Notes first (a new note closes its voice's previous one), then the
    // note-offs that fall inside the interval.
    emitRun (run, from, to);
    flushOffsets (to);
}

void Sequencer::generate (int numSamples, const Transport& transport, const SeqSettings& s,
                          const PatternBank* patternBank, std::vector<SeqEvent>& events)
{
    out = &events;
    bank = patternBank;
    settings = s;
    blockSamples = numSamples;
    beatsPerBar = transport.beatsPerBar > 0.0 ? transport.beatsPerBar : 4.0;
    synced = transport.hostPlaying;

    const auto bpm = synced ? transport.bpm : settings.tempo;
    ppqPerSample = std::max (1.0, bpm) / 60.0 / sampleRate;
    blockStart = synced ? transport.ppq : internalPpq;
    const auto blockEnd = blockStart + numSamples * ppqPerSample;

    // Mode or clock-source changes: stop cleanly. When only the clock changed
    // (the host started or stopped), held or latched keys carry on.
    if (settings.run != lastRun || synced != lastHostPlaying)
    {
        const auto resume = run.active && settings.run == lastRun && settings.run == RunMode::Keys
                         && (numHeld > 0 || settings.latch);
        const auto p = run.pattern;
        const auto v = run.velocityScale * 0.8f;
        stopAt (blockStart);
        lastEnd = -1.0;
        if (resume)
            startOrSwitch (p, v, blockStart);
    }

    // Host jumped (loop, locate): release notes; Keys mode restarts on the new grid.
    auto from = blockStart;
    if (synced && lastEnd >= 0.0)
    {
        if (std::abs (blockStart - lastEnd) < 1.0e-3)
            from = lastEnd; // continuous: pick up exactly where the last block ended
        else
        {
            const auto wasRunning = run.active;
            const auto p = run.pattern;
            const auto v = run.velocityScale;
            stopAt (blockStart);
            if (settings.run == RunMode::Keys && wasRunning && (numHeld > 0 || settings.latch))
                startOrSwitch (p, v * 0.8f, blockStart);
        }
    }

    // Transport mode: run with the host (or the Play button), song-aligned.
    if (settings.run == RunMode::Transport)
    {
        const auto shouldRun = synced || settings.play;
        if (shouldRun && ! run.active)
        {
            run = {};
            run.active = true;
            run.pattern = settings.pattern;
            run.origin = synced ? 0.0 : from; // song-aligned with the host; from Play otherwise
            run.parityBase = 0;
            lastParamPattern = settings.pattern;
        }
        else if (! shouldRun && run.active)
            stopAt (from);

        if (run.active && settings.pattern != lastParamPattern)
        {
            lastParamPattern = settings.pattern;
            startOrSwitch (settings.pattern, 1.0f, from);
        }
    }
    else if (! settings.latch && lastLatch && numHeld == 0 && run.active)
    {
        stopAt (from); // Latch was switched off with no key held
    }

    // Pattern keys, in order, each at its own sample.
    std::sort (commands.begin(), commands.begin() + numCommands,
               [] (const Command& a, const Command& b) { return a.offset < b.offset; });

    for (int i = 0; i < numCommands; ++i)
    {
        const auto& c = commands[(size_t) i];
        const auto t = std::max (from, blockStart + c.offset * ppqPerSample);
        emitInterval (from, t);
        from = t;

        if (settings.run == RunMode::Transport)
        {
            if (c.down && run.active)
                startOrSwitch (c.pattern, 1.0f, t);
            continue;
        }

        if (c.down)
        {
            if (settings.latch && run.active && run.pattern == c.pattern && ! pending.active)
            {
                stopAt (t);
                continue;
            }
            // (Re)start with this pattern, and remember it as held.
            auto* end = held.begin() + numHeld;
            numHeld = (int) (std::remove_if (held.begin(), end, [&] (auto& h) { return h.first == c.pattern; }) - held.begin());
            if (numHeld < kNumPatterns)
                held[(size_t) numHeld++] = { c.pattern, c.velocity };
            startOrSwitch (c.pattern, c.velocity, t);
        }
        else
        {
            const auto wasTop = numHeld > 0 && held[(size_t) numHeld - 1].first == c.pattern;
            auto* end = held.begin() + numHeld;
            numHeld = (int) (std::remove_if (held.begin(), end, [&] (auto& h) { return h.first == c.pattern; }) - held.begin());
            if (settings.latch)
                continue;
            if (numHeld == 0)
                stopAt (t);
            else if (wasTop)
                startOrSwitch (held[(size_t) numHeld - 1].first, held[(size_t) numHeld - 1].second, t);
        }
    }
    numCommands = 0;

    emitInterval (from, blockEnd);

    std::stable_sort (events.begin(), events.end(), [] (const SeqEvent& a, const SeqEvent& b) { return a.offset < b.offset; });

    lastEnd = blockEnd;
    lastHostPlaying = synced;
    lastRun = settings.run;
    lastLatch = settings.latch;
    if (! synced)
        internalPpq = blockEnd;

    uiRunning = run.active;
    uiPattern = run.active ? run.pattern : settings.pattern;
    if (! run.active)
    {
        uiPatternStep = -1;
        for (auto& st : uiTrackStep)
            st = -1;
    }
}

} // namespace batida
