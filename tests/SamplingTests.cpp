#include "Tools.h"

#include "Engine/BreakKit.h"
#include "Engine/Kit.h"
#include "Engine/Render.h"
#include "Engine/Sequencer/PatternMidi.h"
#include "Engine/Sequencer/PatternVary.h"

#include <cmath>

// Phase 8: choke groups, pattern and kit variation, resampling, breaks into
// kits, and patterns out as MIDI.

namespace batida
{

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

float runBlocks (Kit& kit, KitParams& params, int blocks, const juce::MidiBuffer& midi = {})
{
    juce::AudioBuffer<float> out (2, kBlock);
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        kit.setParameters (params);
        kit.process (out, b == 0 ? midi : juce::MidiBuffer());
        peak = std::max (peak, std::max (out.getMagnitude (0, 0, kBlock), out.getMagnitude (1, 0, kBlock)));
    }
    return peak;
}

KitParams quietKit()
{
    auto p = defaultKitParams();
    for (auto& v : p.voices)
        v[vp::ChainAmt] = 0.0f; // dry: only the voices are measured
    return p;
}

// A synthetic drum hit, written into `audio` at `at`.
void addHit (juce::AudioBuffer<float>& audio, int at, HitKind kind, double rate)
{
    juce::Random r (at + 1);
    float lp = 0.0f, hpState = 0.0f;
    const auto len = std::min (audio.getNumSamples() - at, (int) (0.35 * rate));
    for (int i = 0; i < len; ++i)
    {
        const auto t = (float) (i / rate);
        float x = 0.0f;
        switch (kind)
        {
            case HitKind::Kick:
                x = 0.9f * std::sin (6.2831853f * (50.0f * t + 60.0f * (1.0f - std::exp (-t * 30.0f)) / 30.0f)) * std::exp (-t * 12.0f);
                break;
            case HitKind::Snare:
            {
                // Mostly noise (the wires), with a short tonal body.
                const auto n = r.nextFloat() * 2.0f - 1.0f;
                lp += 0.7f * (n - lp);
                x = (0.7f * lp + 0.35f * std::sin (6.2831853f * 190.0f * t) * std::exp (-t * 40.0f)) * std::exp (-t * 20.0f);
                break;
            }
            case HitKind::ClosedHat:
            case HitKind::OpenHat:
            {
                const auto n = r.nextFloat() * 2.0f - 1.0f;
                const auto hp = n - hpState;
                hpState = n;
                x = 0.5f * hp * std::exp (-t * (kind == HitKind::OpenHat ? 6.0f : 60.0f));
                break;
            }
            case HitKind::Perc:
                x = 0.6f * std::sin (6.2831853f * 700.0f * t) * std::exp (-t * 25.0f);
                break;
        }
        audio.addSample (0, at + i, x);
    }
}
} // namespace

class SamplingTests final : public juce::UnitTest
{
public:
    SamplingTests() : juce::UnitTest ("Sampling and variation", "Batida") {}

    void runTest() override
    {
        beginTest ("A hit cuts the other sounds in its choke group");
        {
            auto hat = [&] (bool choke)
            {
                Kit kit;
                kit.prepare (kRate, kBlock);
                auto params = quietKit();
                params.voices[6][vp::Choke] = choke ? 1.0f : 0.0f;
                params.voices[7][vp::Choke] = choke ? 1.0f : 0.0f;
                for (int v = 0; v < 6; ++v)
                    params.voices[(size_t) v][vp::Mute] = 1.0f;
                params.voices[6][vp::Mute] = 1.0f; // hear only the open hat
                runBlocks (kit, params, 1);
                kit.noteOn (7, Voice::kBaseKey, 1.0f); // open hat rings
                runBlocks (kit, params, 8);
                kit.noteOn (6, Voice::kBaseKey, 1.0f); // closed hat
                runBlocks (kit, params, 2);
                return runBlocks (kit, params, 4);
            };
            const auto choked = hat (true), free = hat (false);
            logMessage ("  open hat after the closed hat: choked " + juce::String (choked, 4) + ", free " + juce::String (free, 4));
            expectLessThan (choked, 1.0e-4f, "the open hat stops");
            expectGreaterThan (free, 0.01f, "without a group it rings on");
        }

        beginTest ("Pattern Vary: four different suggestions, locks and empty tracks kept");
        {
            PatternVaryRequest req;
            req.base = breakbeatPattern();
            req.amount = 0.5f;
            req.locked[0] = true; // kick
            req.seed = 42;
            const auto cands = varyPattern (req);
            expectEquals ((int) cands.size(), 4);
            for (size_t i = 0; i < cands.size(); ++i)
            {
                const auto& p = cands[i].pattern;
                expect (patternDistance (p, req.base) >= 2, "differs from the original");
                expect (! cands[i].note.empty(), "says what changed");
                for (int s = 0; s < kMaxSteps; ++s)
                    expect (p.tracks[0].steps[(size_t) s].gate == req.base.tracks[0].steps[(size_t) s].gate, "the locked kick is untouched");
                for (int t = 0; t < kNumTracks; ++t)
                {
                    bool baseHas = false, has = false;
                    for (int s = 0; s < kMaxSteps; ++s)
                    {
                        baseHas = baseHas || req.base.tracks[(size_t) t].steps[(size_t) s].gate;
                        has = has || p.tracks[(size_t) t].steps[(size_t) s].gate;
                    }
                    if (! baseHas)
                        expect (! has, "an empty track stays empty");
                }
                for (size_t j = 0; j < i; ++j)
                    expect (patternDistance (p, cands[j].pattern) >= 2, "no near-duplicates");
            }
            for (const auto& c : cands)
                logMessage ("  " + juce::String::fromUTF8 (c.note.c_str()));
        }

        beginTest ("Pattern Vary directions do what they say");
        {
            auto hits = [] (const Pattern& p)
            {
                int n = 0;
                for (const auto& t : p.tracks)
                    for (int s = 0; s < p.length; ++s)
                        n += t.steps[(size_t) s].gate ? 1 : 0;
                return n;
            };
            auto ratchets = [] (const Pattern& p)
            {
                int n = 0;
                for (const auto& t : p.tracks)
                    for (int s = 0; s < p.length; ++s)
                        n += t.steps[(size_t) s].gate && t.steps[(size_t) s].ratchet > 1 ? 1 : 0;
                return n;
            };
            PatternVaryRequest req;
            req.base = breakbeatPattern();
            req.amount = 0.6f;
            req.seed = 7;
            req.direction = PatternDirection::Denser;
            for (const auto& c : varyPattern (req))
                expectGreaterThan (hits (c.pattern), hits (req.base), "denser adds hits");
            req.direction = PatternDirection::Sparser;
            for (const auto& c : varyPattern (req))
                expectLessThan (hits (c.pattern), hits (req.base), "sparser removes hits");
            req.direction = PatternDirection::Rolls;
            for (const auto& c : varyPattern (req))
                expectGreaterThan (ratchets (c.pattern), ratchets (req.base), "rolls add ratchets");
        }

        beginTest ("A held pattern preview plays without touching the real patterns");
        {
            Kit kit;
            kit.prepare (kRate, kBlock);
            auto params = quietKit();
            params.global[gp::SeqPattern] = 2.0f;
            // Pattern 3 is empty; the preview has a kick on every beat.
            PatternPreview pv;
            pv.active = true;
            pv.bank = kit.patternStore().get();
            for (int s = 0; s < 16; s += 4)
                pv.bank.patterns[2].tracks[0].steps[(size_t) s].gate = true;
            kit.patternPreview().replace (pv);
            const auto before = kit.getHitCount (0);
            kit.auditionPattern (2, true);
            runBlocks (kit, params, 200); // ~1 s at 120 bpm: two beats or more
            expectGreaterThan ((int) (kit.getHitCount (0) - before), 1, "the preview plays while held");
            kit.auditionPattern (2, false);
            runBlocks (kit, params, 2);
            const auto after = kit.getHitCount (0);
            runBlocks (kit, params, 200);
            expectEquals ((int) kit.getHitCount (0), (int) after, "letting go stops it");
            expect (kit.patternStore().get().patterns[2].isEmpty(), "the real pattern is untouched");
        }

        beginTest ("XY CCs move the pad until the pad itself moves");
        {
            Kit kit;
            kit.prepare (kRate, kBlock);
            auto params = quietKit();
            params.global[gp::XyX] = 0.5f;
            params.global[gp::XyY] = 0.0f;
            runBlocks (kit, params, 1);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::controllerEvent (3, kXyCcY, 127), 0);
            runBlocks (kit, params, 1, midi);
            expect (kit.isXyLocked());
            expectWithinAbsoluteError (kit.getLockY(), 1.0f, 1.0e-4f);
            expectWithinAbsoluteError (kit.getLockX(), 0.5f, 1.0e-4f, "X stays where the pad is");
            params.global[gp::XyX] = 0.6f; // the pad moves
            runBlocks (kit, params, 1);
            expect (! kit.isXyLocked(), "the pad takes back over");
        }

        beginTest ("MIDI export: drum-map notes, beats, rolls written out, XY as CCs");
        {
            PatternBank bank;
            auto& p = bank.patterns[4];
            p.length = 16;
            p.tracks[0].steps[0].gate = true;
            p.tracks[0].steps[0].velocity = 127;
            p.tracks[2].steps[4].gate = true;
            p.tracks[2].steps[4].ratchet = 3;
            p.tracks[6].steps[2].gate = true;
            p.tracks[6].steps[2].probability = 0; // never plays
            p.xy[8] = { true, 1.0f, 0.5f };
            MidiExportOptions opt;
            opt.padX = 0.25f;
            opt.padY = 0.0f;
            const auto file = patternToMidi (bank, 4, opt);
            expectEquals (file.getNumTracks(), 1);
            expectEquals ((int) file.getTimeFormat(), kMidiTicksPerBeat);
            const auto& track = *file.getTrack (0);
            std::vector<std::pair<double, int>> ons;
            std::vector<std::tuple<double, int, int>> ccs;
            bool tempo = false;
            for (const auto* e : track)
            {
                const auto& m = e->message;
                tempo = tempo || m.isTempoMetaEvent();
                if (m.isNoteOn())
                {
                    ons.push_back ({ m.getTimeStamp() / kMidiTicksPerBeat, m.getNoteNumber() });
                    expectEquals (m.getChannel(), kMidiDrumChannel);
                }
                if (m.isController())
                    ccs.push_back ({ m.getTimeStamp() / kMidiTicksPerBeat, m.getControllerNumber(), m.getControllerValue() });
            }
            expect (! tempo, "no tempo in the file");
            expectEquals ((int) ons.size(), 1 + 3, "kick + three snare rolls, no hat");
            expectWithinAbsoluteError (ons[0].first, 0.0, 1.0e-3);
            expectEquals (ons[0].second, kDrumMapFirstNote);
            int snares = 0;
            for (const auto& [beat, note] : ons)
                if (note == kDrumMapFirstNote + 2)
                {
                    expect (beat >= 1.0 - 1.0e-3 && beat < 1.25, "the roll fills step 5");
                    ++snares;
                }
            expectEquals (snares, 3);
            bool lock = false, back = false;
            for (const auto& [beat, number, value] : ccs)
            {
                if (std::abs (beat - 2.0) < 1.0e-3 && number == kXyCcX && value == 127)
                    lock = true;
                if (std::abs (beat - 2.25) < 1.0e-3 && number == kXyCcX && value == 32)
                    back = true;
            }
            expect (lock, "the lock on step 9 is CC 16 = 127");
            expect (back, "the next step lets go back to the pad");
            expectWithinAbsoluteError (track.getEndTime() / kMidiTicksPerBeat, 4.0, 1.0e-3, "one pass of 16 steps");
        }

        beginTest ("Resampling a sound renders its hit; a pattern renders one seamless pass");
        {
            RenderRequest req;
            req.params = defaultKitParams();
            req.movement = defaultMovement();
            req.sampleRate = kRate;
            req.voice = 0;
            const auto hit = renderKit (req);
            expect (hit.getNumSamples() > (int) (0.1 * kRate) && hit.getNumSamples() < (int) (6.1 * kRate));
            expectGreaterThan (hit.getMagnitude (0, 0, std::min (4800, hit.getNumSamples())), 0.05f, "the hit starts at once");

            req.mode = RenderRequest::Mode::Pattern;
            req.bank.patterns[0] = breakbeatPattern();
            req.pattern = 0;
            req.bpm = 120.0;
            const auto loop = renderKit (req);
            expectEquals (loop.getNumSamples(), patternPassSamples (req.bank.patterns[0], 120.0, kRate));
            expectGreaterThan (loop.getMagnitude (0, 0, loop.getNumSamples()), 0.05f);
        }

        beginTest ("Hits are told apart: kick, snare, closed and open hat, tonal percussion");
        {
            const auto rate = 44100.0;
            for (const auto kind : { HitKind::Kick, HitKind::Snare, HitKind::ClosedHat, HitKind::OpenHat, HitKind::Perc })
            {
                SampleData d;
                d.sampleRate = rate;
                d.audio.setSize (1, (int) (0.5 * rate));
                d.audio.clear();
                addHit (d.audio, 0, kind, rate);
                const auto f = analyseHit (d, 0, d.numFrames());
                logMessage ("  kind " + juce::String ((int) kind) + ": low " + juce::String (f.low, 2) + " bright " + juce::String (f.bright, 2)
                            + " high " + juce::String (f.high, 2) + " decay " + juce::String (f.decayMs, 0) + " ms periodic " + juce::String (f.periodic, 2));
                expectEquals ((int) classifyHit (f), (int) kind);
            }
        }

        beginTest ("A break becomes a kit: tempo, slots and a pattern that replays it");
        {
            // Two bars at 120 bpm: kick, hat, snare, hat… with an open hat at the end.
            const auto rate = 44100.0;
            const auto frames = (int) (4.0 * rate);
            const auto step = frames / 32.0;
            SampleData data;
            data.sampleRate = rate;
            data.audio.setSize (1, frames);
            data.audio.clear();
            struct Want { int step; HitKind kind; };
            const std::vector<Want> wants { { 0, HitKind::Kick }, { 4, HitKind::ClosedHat }, { 8, HitKind::Snare }, { 12, HitKind::ClosedHat },
                                            { 16, HitKind::Kick }, { 20, HitKind::ClosedHat }, { 24, HitKind::Snare }, { 28, HitKind::OpenHat } };
            for (const auto& w : wants)
                addHit (data.audio, (int) (w.step * step), w.kind, rate);
            SampleSlot slot; // computes the onsets as a load would
            auto owned = std::make_unique<SampleData> (data);
            owned->path = "/tmp/break.wav";
            slot.setData (std::move (owned));
            const auto plan = planBreak (*slot.getDisplayData());
            expect (plan.has_value());
            if (! plan)
                return;
            logMessage ("  " + juce::String (plan->bars) + " bars, " + juce::String (plan->bpm, 1) + " bpm, "
                        + juce::String ((int) plan->hits.size()) + " hits");
            for (const auto& h : plan->hits)
                logMessage ("  hit step " + juce::String (h.step + 1) + " kind " + juce::String ((int) h.kind) + " slice " + juce::String (h.slice));
            expectEquals (plan->bars, 2);
            expectWithinAbsoluteError (plan->bpm, 120.0, 0.5);
            expectEquals (plan->pattern.length, 32);
            for (const auto& w : wants)
            {
                const auto slotIndex = slotFor (w.kind);
                const auto& st = plan->pattern.tracks[(size_t) slotIndex].steps[(size_t) w.step];
                expect (st.gate, "step " + juce::String (w.step + 1) + " is on slot " + juce::String (slotIndex + 1));
            }
            expect (plan->used[0] && plan->used[2] && plan->used[6], "kick, snare and hat slots are used");
            expect (! plan->used[5], "the bass slot is left alone");
            expectEquals ((int) plan->voices[0][vp::SmpSliceMode], (int) SliceMode::Transients);
            expectEquals (plan->voices[6].choice (vp::Choke), 1, "the hats choke each other");

            // Each hit's slice starts where the hit is.
            SliceSpec spec;
            spec.mode = SliceMode::Transients;
            spec.sensitivity = plan->sensitivity;
            for (const auto& h : plan->hits)
            {
                const auto [from, to] = sliceRegion (*slot.getDisplayData(), spec, 0.0, (double) frames, h.slice);
                expectLessThan (std::abs (from - h.frame), 1.0, "slice " + juce::String (h.slice) + " starts on its hit");
            }
        }
    }
};

static SamplingTests samplingTests;

} // namespace batida
