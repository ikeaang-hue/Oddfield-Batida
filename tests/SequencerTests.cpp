#include "Tools.h"

#include <cmath>
#include <functional>

namespace batida
{

namespace
{
constexpr double kRate = 48000.0;

struct Timed
{
    long long at;
    SeqEvent e;
};

struct KeyPress
{
    long long at;
    bool down;
    int pattern;
    float velocity = 0.8f;
};

// Runs the sequencer alone with a simulated host. ppqAt(sample) gives the
// host position at each block start (default: playing from 0 at `bpm`).
std::vector<Timed> runSequencer (const PatternBank& bank, SeqSettings s, bool hostPlaying, double bpm, long long total,
                                 int block, std::vector<KeyPress> keys = {},
                                 std::function<double (long long)> ppqAt = nullptr)
{
    Sequencer seq;
    seq.prepare (kRate);
    std::vector<SeqEvent> ev;
    std::vector<Timed> out;

    for (long long pos = 0; pos < total; pos += block)
    {
        const auto n = (int) std::min<long long> (block, total - pos);
        for (const auto& k : keys)
            if (k.at >= pos && k.at < pos + n)
            {
                if (k.down)
                    seq.keyDown (k.pattern, k.velocity, (int) (k.at - pos));
                else
                    seq.keyUp (k.pattern, (int) (k.at - pos));
            }

        Transport t;
        t.hostPlaying = hostPlaying;
        t.bpm = bpm;
        t.ppq = ppqAt ? ppqAt (pos) : (double) pos * bpm / 60.0 / kRate;
        ev.clear();
        seq.generate (n, t, s, &bank, ev);
        for (const auto& e : ev)
            out.push_back ({ pos + e.offset, e });
    }
    return out;
}

std::vector<long long> onsets (const std::vector<Timed>& events, int voice = 0)
{
    std::vector<long long> r;
    for (const auto& t : events)
        if (t.e.type == SeqEvent::Type::NoteOn && t.e.voice == voice)
            r.push_back (t.at);
    return r;
}

PatternBank bankWith (std::function<void (Pattern&)> fill)
{
    PatternBank b;
    fill (b.patterns[0]);
    return b;
}

PatternBank fourOnTheFloor()
{
    return bankWith ([] (Pattern& p)
    {
        for (int s = 0; s < 16; s += 4)
            p.tracks[0].steps[(size_t) s].gate = true;
    });
}

SeqSettings transportMode()
{
    SeqSettings s;
    s.run = RunMode::Transport;
    return s;
}

// 120 bpm at 48 kHz: a 16th is 6000 samples.
constexpr long long k16th = 6000;
} // namespace

class SequencerTests final : public juce::UnitTest
{
public:
    SequencerTests() : juce::UnitTest ("Sequencer", "Batida") {}

    void runTest() override
    {
        beginTest ("Hits land on their exact sample, whatever the block size");
        for (const auto block : { 512, 100, 4096, 1 })
        {
            const auto hits = onsets (runSequencer (fourOnTheFloor(), transportMode(), true, 120.0, 100000, block));
            juce::String h;
            for (auto x : hits) h << (int) x << " ";
            logMessage ("  block " + juce::String (block) + ": " + h);
            expect (hits == std::vector<long long> { 0, 24000, 48000, 72000, 96000 }, "block " + juce::String (block));
        }
        {
            // 140 bpm at 44.1 kHz: a 16th is 4725 samples.
            Sequencer seq;
            seq.prepare (44100.0);
            std::vector<SeqEvent> ev;
            std::vector<long long> hits;
            for (long long pos = 0; pos < 44100; pos += 256)
            {
                Transport t;
                t.hostPlaying = true;
                t.bpm = 140.0;
                t.ppq = (double) pos * 140.0 / 60.0 / 44100.0;
                ev.clear();
                seq.generate (256, t, transportMode(), &static_cast<const PatternBank&> (fourOnTheFloor()), ev);
                for (const auto& e : ev)
                    if (e.type == SeqEvent::Type::NoteOn)
                        hits.push_back (pos + e.offset);
            }
            expect (hits == std::vector<long long> { 0, 18900, 37800 }, "140 bpm at 44.1 kHz");
        }

        beginTest ("Swing delays the off-beat 16ths");
        {
            const auto bank = bankWith ([] (Pattern& p) { p.tracks[0].steps[1].gate = p.tracks[0].steps[2].gate = true; });
            auto s = transportMode();
            s.swing = 0.75f;
            const auto hits = onsets (runSequencer (bank, s, true, 120.0, 20000, 512));
            expect (hits == std::vector<long long> { k16th + 3000, 2 * k16th }, "swung off-beat, straight on-beat");
        }

        beginTest ("Polymeter: a 3-step track loops inside a 16-step pattern");
        {
            const auto bank = bankWith ([] (Pattern& p)
            {
                p.tracks[0].length = 3;
                p.tracks[0].steps[0].gate = true;
            });
            const auto hits = onsets (runSequencer (bank, transportMode(), true, 120.0, 16 * k16th, 512));
            expect (hits == std::vector<long long> { 0, 3 * k16th, 6 * k16th, 9 * k16th, 12 * k16th, 15 * k16th });
        }

        beginTest ("Ratchets split a step evenly");
        {
            const auto bank = bankWith ([] (Pattern& p)
            {
                p.tracks[0].steps[0].gate = true;
                p.tracks[0].steps[0].ratchet = 4;
            });
            const auto hits = onsets (runSequencer (bank, transportMode(), true, 120.0, k16th, 512));
            expect (hits == std::vector<long long> { 0, 1500, 3000, 4500 });
        }

        beginTest ("Probability: about right, and identical on every playback");
        {
            const auto bank = bankWith ([] (Pattern& p)
            {
                for (auto& st : p.tracks[0].steps)
                {
                    st.gate = true;
                    st.probability = 50;
                }
            });
            const auto a = onsets (runSequencer (bank, transportMode(), true, 120.0, 1024 * k16th, 512));
            const auto b = onsets (runSequencer (bank, transportMode(), true, 120.0, 1024 * k16th, 333));
            logMessage ("  " + juce::String ((int) a.size()) + " of 1024 steps played at 50%");
            expect (std::abs ((int) a.size() - 512) < 60);
            expect (a == b, "same hits on replay (and with other block sizes)");
        }

        beginTest ("Host loop: jumping back replays the start without doubling");
        {
            // Plays ppq 0..2, then the host loops back to 0 and plays 0..2 again.
            const auto loopLen = 48000LL; // 2 quarter notes
            const auto hits = onsets (runSequencer (fourOnTheFloor(), transportMode(), true, 120.0, 2 * loopLen, 512, {},
                                                    [&] (long long pos) { return (double) (pos % loopLen) * 2.0 / 48000.0; }));
            logMessage ("  hits: " + juce::String ((int) hits.size()));
            expectEquals ((int) hits.size(), 4); // steps 0 and 4, twice
            expect (hits.size() == 4 && hits[2] >= loopLen && hits[2] < loopLen + 512, "restart right after the jump");
        }

        beginTest ("Pattern key held: quantised start, stops on release");
        {
            SeqSettings s; // Keys, Beat quantise
            const auto events = runSequencer (fourOnTheFloor(), s, true, 120.0, 96000, 512,
                                              { { 3600, true, 0 }, { 50400, false, 0 } });
            const auto hits = onsets (events);
            expect (hits == std::vector<long long> { 24000, 48000 }, "starts on the next beat, stops at release");
            bool releasedAtStop = false;
            for (const auto& t : events)
                if (t.e.type == SeqEvent::Type::NoteOff && t.at >= 50400 && t.at < 50400 + 512)
                    releasedAtStop = true;
            expect (releasedAtStop, "notes are released when the key comes up");
        }

        beginTest ("Latch: release keeps it going, a second press stops it");
        {
            SeqSettings s;
            s.latch = true;
            const auto hits = onsets (runSequencer (fourOnTheFloor(), s, true, 120.0, 120000, 512,
                                                    { { 0, true, 0 }, { 100, false, 0 }, { 60000, true, 0 }, { 60100, false, 0 } }));
            expect (hits == std::vector<long long> { 0, 24000, 48000 });
        }

        beginTest ("Host stopped: a key starts the pattern right away on the internal tempo");
        {
            SeqSettings s;
            s.tempo = 120.0;
            const auto hits = onsets (runSequencer (fourOnTheFloor(), s, false, 0.0, 60000, 512, { { 1000, true, 0 } }));
            expect (hits == std::vector<long long> { 1000, 25000, 49000 });
        }

        beginTest ("Sync off: the host's tempo and position are ignored");
        {
            SeqSettings s;
            s.sync = false;
            s.tempo = 60.0; // a 16th = 12000 samples
            // Host plays at 120 bpm, but the key starts at once on our 60 bpm.
            const auto hits = onsets (runSequencer (fourOnTheFloor(), s, true, 120.0, 120000, 512, { { 3600, true, 0 } }));
            expect (hits == std::vector<long long> { 3600, 51600, 99600 });
        }

        beginTest ("Holding another key switches pattern at the next bar");
        {
            auto bank = fourOnTheFloor();
            bank.patterns[1].tracks[2].steps[0].gate = true; // pattern 2: snare on 1
            SeqSettings s;
            s.quantise = Quantise::Bar;
            const auto events = runSequencer (bank, s, true, 120.0, 200000, 512, { { 0, true, 0 }, { 30000, true, 1 } });
            expect (onsets (events, 0) == std::vector<long long> { 0, 24000, 48000, 72000 }, "pattern 1 until the bar");
            expect (onsets (events, 2) == std::vector<long long> { 96000, 192000 }, "pattern 2 from the bar");
        }

        beginTest ("XY lane: a lock lasts one step, then snaps back");
        {
            const auto bank = bankWith ([] (Pattern& p) { p.xy[2] = { true, 0.9f, 0.8f }; });
            const auto events = runSequencer (bank, transportMode(), true, 120.0, 5 * k16th, 512);
            std::vector<std::pair<long long, bool>> xy;
            for (const auto& t : events)
                if (t.e.type == SeqEvent::Type::XyLock || t.e.type == SeqEvent::Type::XyRelease)
                    xy.push_back ({ t.at, t.e.type == SeqEvent::Type::XyLock });
            expect (xy == std::vector<std::pair<long long, bool>> { { 2 * k16th, true }, { 3 * k16th, false } });
        }

        beginTest ("Chromatic mode: pattern keys only on channel 10");
        {
            auto params = defaultKitParams();
            params.global[gp::MidiMode] = 1.0f;
            Kit kit;
            kit.setParameters (params);
            kit.prepare (kRate, 512);
            juce::AudioBuffer<float> buf (2, 512);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            kit.process (buf, midi);
            expect (! kit.getSequencer().isRunning(), "channel 1 plays the Keys Voice");

            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (10, 60, 0.8f), 0);
            kit.setParameters (params);
            kit.process (buf, midi);
            expect (kit.getSequencer().isRunning(), "channel 10 plays pattern 1");
        }

        beginTest ("Through the kit: holding C3 plays the breakbeat");
        {
            auto params = defaultKitParams();
            Kit kit;
            kit.patternStore().edit ([] (PatternBank& b) { b.patterns[0] = breakbeatPattern(); });
            kit.setParameters (params);
            kit.prepare (kRate, 512);
            juce::AudioBuffer<float> buf (2, 512);
            double energy = 0.0;
            for (int b = 0; b < 200; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
                kit.setParameters (params);
                kit.process (buf, midi);
                for (int i = 0; i < 512; ++i)
                    energy += (double) buf.getSample (0, i) * buf.getSample (0, i);
            }
            expectGreaterThan (energy, 1.0);
        }
    }
};

class SliceTests final : public juce::UnitTest
{
public:
    SliceTests() : juce::UnitTest ("Slices", "Batida") {}

    void runTest() override
    {
        beginTest ("Grid slices: slice 3 of 4 plays a quarter of the sample");
        {
            auto data = std::make_unique<SampleData>();
            data->audio.setSize (1, 4800);
            for (int i = 0; i < 4800; ++i)
                data->audio.setSample (0, i, 0.3f * std::sin (0.05f * (float) i));
            data->sampleRate = kRate;

            auto p = defaultKitParams();
            auto& v = p.voices[0];
            v[vp::SrcMode] = 1.0f;
            v[vp::SmpSliceMode] = 1.0f;
            v[vp::SmpSlices] = 4.0f;
            v[vp::AmpD] = 8000.0f;
            v[vp::AmpS] = 1.0f;
            v[vp::PitchAmt] = 0.0f;
            v[vp::ChainAmt] = 0.0f;

            Kit kit;
            kit.prepare (kRate, 1);
            kit.sampleSlot (0).setData (std::move (data));
            juce::AudioBuffer<float> buf (2, 1);
            kit.setParameters (p);
            kit.process (buf, {});
            kit.noteOn (0, 60, 1.0f, 2);
            int n = 0;
            while (kit.getVoice (0).isActive() && n < 10000)
            {
                kit.setParameters (p);
                kit.process (buf, {});
                ++n;
            }
            expectWithinAbsoluteError (n, 1200, 3);
        }

        beginTest ("Transient slices: onsets found at the hits");
        {
            auto data = std::make_unique<SampleData>();
            data->audio.setSize (1, 48000);
            data->audio.clear();
            juce::Random rng (1);
            for (const auto at : { 0, 12000, 24000, 36000 })
                for (int i = 0; i < 3000; ++i)
                    data->audio.setSample (0, at + i, (rng.nextFloat() * 2.0f - 1.0f) * std::exp (-i / 600.0f));
            data->sampleRate = kRate;

            SampleSlot slot;
            slot.setData (std::move (data));
            const auto* d = slot.getDisplayData();
            juce::String found;
            for (const auto& [frame, strength] : d->onsets)
                found << frame << " ";
            logMessage ("  onsets: " + found);

            SliceSpec spec;
            spec.mode = SliceMode::Transients;
            spec.sensitivity = 0.5f;
            expectEquals (sliceCount (*d, spec, 0.0, 48000.0), 4);
            for (int k = 1; k < 4; ++k)
                expectWithinAbsoluteError (sliceRegion (*d, spec, 0.0, 48000.0, k).first, 12000.0 * k, 64.0);
        }

        beginTest ("Patterns survive XML; the default bank is empty (the demo: pattern 1 only)");
        {
            expect (defaultPatternBank().patterns[0].isEmpty() != kShipDemoPattern);
            PatternBank bank = defaultPatternBank();
            bank.patterns[0] = breakbeatPattern();
            for (int p = 1; p < kNumPatterns; ++p)
                expect (bank.patterns[(size_t) p].isEmpty());

            PatternBank back;
            back.fromXml (*bank.toXml());
            const auto& a = bank.patterns[0];
            const auto& b = back.patterns[0];
            bool same = a.length == b.length;
            for (int t = 0; t < kNumTracks; ++t)
                for (int s = 0; s < kMaxSteps; ++s)
                {
                    const auto& x = a.tracks[(size_t) t].steps[(size_t) s];
                    const auto& y = b.tracks[(size_t) t].steps[(size_t) s];
                    same = same && x.gate == y.gate && x.velocity == y.velocity && x.pitch == y.pitch
                        && x.ratchet == y.ratchet && x.probability == y.probability && x.slice == y.slice;
                }
            same = same && b.xy[12].active && std::abs (b.xy[12].x - a.xy[12].x) < 1.0e-3f;
            expect (same);
        }
    }
};

static SequencerTests sequencerTests;
static SliceTests sliceTests;

} // namespace batida
