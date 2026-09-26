#include "Tools.h"

#include <cmath>
#include <set>

namespace batida
{

namespace
{
constexpr double kRate = 48000.0;

// A plain voice: one sine carrier, envelopes long enough to not end the note.
VoiceParams plainVoice()
{
    VoiceParams p;
    for (int i = 0; i < kNumVoiceParams; ++i)
        p[i] = voiceParamSpecs()[(size_t) i].def;
    p[vp::AmpD] = 8000.0f;
    p[vp::AmpS] = 1.0f;
    p[vp::AmpR] = 8000.0f;
    p[vp::VelSens] = 0.0f;
    p[vp::ChainAmt] = 0.0f; // voice tests hear the voice alone
    return p;
}

KitParams kitWith (const VoiceParams& voice)
{
    auto kit = defaultKitParams();
    kit.voices[0] = voice;
    return kit;
}

int samplesUntilIdle (Kit& kit, const KitParams& params, int maxSamples)
{
    juce::AudioBuffer<float> buf (2, 1);
    juce::MidiBuffer midi;
    for (int n = 0; n < maxSamples; ++n)
    {
        kit.setParameters (params);
        kit.process (buf, midi);
        if (! kit.getVoice (0).isActive())
            return n;
    }
    return maxSamples;
}
} // namespace

class EnvelopeTests final : public juce::UnitTest
{
public:
    EnvelopeTests() : juce::UnitTest ("Envelope", "Batida") {}

    void runTest() override
    {
        beginTest ("One-shot decay falls 60 dB over the decay time");
        {
            Envelope env;
            env.setSampleRate (kRate);
            env.setParameters (0.0f, 100.0f, 0.0f, 50.0f);
            env.trigger (true);
            float v = 0.0f;
            for (int i = 0; i < 4800; ++i)
                v = env.next();
            expectWithinAbsoluteError (20.0f * std::log10 (v), -60.0f, 1.0f);
            for (int i = 0; i < 4800; ++i)
                env.next();
            expect (! env.isActive(), "should finish after the tail");
        }

        beginTest ("One-shot ignores note-off; two-stage decay via sustain");
        {
            Envelope env;
            env.setSampleRate (kRate);
            env.setParameters (0.0f, 10.0f, 0.5f, 200.0f);
            env.trigger (true);
            env.release(); // ignored
            for (int i = 0; i < 960; ++i) // 20 ms: decay done, release under way
                env.next();
            expect (env.getStage() == Envelope::Stage::Release);
            expect (env.getValue() < 0.5f && env.getValue() > 0.2f);
        }

        beginTest ("Gate holds sustain until released");
        {
            Envelope env;
            env.setSampleRate (kRate);
            env.setParameters (1.0f, 50.0f, 0.5f, 50.0f);
            env.trigger (false);
            for (int i = 0; i < 9600; ++i)
                env.next();
            expectWithinAbsoluteError (env.getValue(), 0.5f, 1.0e-3f);
            env.release();
            float v = 0.0f;
            for (int i = 0; i < 2400; ++i)
                v = env.next();
            expectWithinAbsoluteError (20.0f * std::log10 (v / 0.5f), -60.0f, 1.0f);
        }
    }
};

class VoiceTests final : public juce::UnitTest
{
public:
    VoiceTests() : juce::UnitTest ("Voice", "Batida") {}

    void runTest() override
    {
        beginTest ("Glide reaches 99% of the interval in the glide time");
        {
            auto p = plainVoice();
            p[vp::PlayMode] = 1.0f;
            p[vp::Glide] = 100.0f;

            Voice voice;
            voice.prepare (kRate);
            voice.setParameters (p);
            std::vector<float> l (4800), r (4800);
            voice.noteOn (60, 1.0f);
            voice.render (l.data(), r.data(), l.data(), r.data(), 480);
            voice.noteOn (72, 1.0f);
            voice.render (l.data(), r.data(), l.data(), r.data(), 4800);
            expectWithinAbsoluteError (voice.getPitchOffset(), 12.0f * 0.99f, 0.05f);

            voice.noteOff (72); // back to the held note, legato
            voice.render (l.data(), r.data(), l.data(), r.data(), 4800);
            expectWithinAbsoluteError (voice.getPitchOffset(), 0.12f, 0.05f);
            expect (voice.isActive());
        }

        beginTest ("Retrigger without glide fades out instead of clicking");
        {
            auto p = plainVoice();
            Voice voice;
            voice.prepare (kRate);
            voice.setParameters (p);

            std::vector<float> l (4800, 0.0f), r (4800, 0.0f);
            voice.noteOn (60, 1.0f);
            voice.render (l.data(), r.data(), l.data(), r.data(), 1234); // mid-cycle
            voice.noteOn (60, 1.0f);
            voice.render (l.data() + 1234, r.data() + 1234, l.data() + 1234, r.data() + 1234, 4800 - 1234);

            float maxJump = 0.0f;
            for (int i = 1; i < 4800; ++i)
                maxJump = std::max (maxJump, std::abs (l[(size_t) i] - l[(size_t) i - 1]));

            // A 261 Hz sine at full scale moves at most ~0.034 per sample.
            expectLessThan (maxJump, 0.05f);
        }

        beginTest ("Voice ends when its one-shot envelope does");
        {
            auto p = plainVoice();
            p[vp::AmpD] = 50.0f;
            p[vp::AmpS] = 0.0f;
            Kit kit;
            kit.prepare (kRate, 1);
            kit.setParameters (kitWith (p));
            kit.noteOn (0, 60, 1.0f);
            const auto n = samplesUntilIdle (kit, kitWith (p), 48000);
            expect (n > 2400 && n < 4800, "ended after " + juce::String (n) + " samples");
        }
    }
};

class MidiTests final : public juce::UnitTest
{
public:
    MidiTests() : juce::UnitTest ("MIDI routing", "Batida") {}

    void runTest() override
    {
        VoiceEvent e;

        beginTest ("Drum map: notes 36-43 on any channel");
        expect (routeMidi (juce::MidiMessage::noteOn (3, 38, 0.5f), MidiMode::DrumMap, 0, e));
        expectEquals (e.voice, 2);
        expectEquals (e.key, 60);
        expect (e.isNoteOn);
        expect (! routeMidi (juce::MidiMessage::noteOn (1, 44, 0.5f), MidiMode::DrumMap, 0, e));
        expect (! routeMidi (juce::MidiMessage::noteOn (1, 35, 0.5f), MidiMode::DrumMap, 0, e));
        expect (routeMidi (juce::MidiMessage::noteOff (5, 43), MidiMode::DrumMap, 0, e));
        expectEquals (e.voice, 7);
        expect (! e.isNoteOn);

        beginTest ("Chromatic: channel 10 drum map, every other channel plays the keys voice");
        expect (routeMidi (juce::MidiMessage::noteOn (10, 36, 1.0f), MidiMode::Chromatic, 5, e));
        expectEquals (e.voice, 0);
        expectEquals (e.key, 60);
        expect (routeMidi (juce::MidiMessage::noteOn (2, 64, 1.0f), MidiMode::Chromatic, 5, e));
        expectEquals (e.voice, 5);
        expectEquals (e.key, 64);
        expect (routeMidi (juce::MidiMessage::noteOn (16, 30, 1.0f), MidiMode::Chromatic, 5, e));
        expectEquals (e.voice, 5);
        expectEquals (e.key, 30);
        expect (! routeMidi (juce::MidiMessage::noteOn (10, 60, 1.0f), MidiMode::Chromatic, 5, e));

        beginTest ("Changing the keys voice releases held notes");
        {
            auto params = defaultKitParams();
            params.global[gp::MidiMode] = 1.0f;
            params.global[gp::KeysVoice] = 5.0f; // bass, Gate mode
            Kit kit;
            kit.prepare (48000.0, 128);
            kit.setParameters (params);
            juce::AudioBuffer<float> buf (2, 128);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 48, 1.0f), 0);
            kit.process (buf, midi);
            expect (kit.getVoice (5).isActive());

            params.global[gp::KeysVoice] = 0.0f;
            midi.clear();
            for (int i = 0; i < 100; ++i) // ~0.27 s: the bass releases (120 ms)
            {
                kit.setParameters (params);
                kit.process (buf, midi);
            }
            expect (! kit.getVoice (5).isActive(), "bass should have been released");
        }

        beginTest ("Velocity 0 note-on is a note-off");
        expect (routeMidi (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 0), MidiMode::DrumMap, 0, e));
        expect (! e.isNoteOn);
    }
};

class SampleTests final : public juce::UnitTest
{
public:
    SampleTests() : juce::UnitTest ("Sample playback", "Batida") {}

    static std::unique_ptr<SampleData> sine (int frames)
    {
        auto data = std::make_unique<SampleData>();
        data->audio.setSize (1, frames);
        for (int i = 0; i < frames; ++i)
            data->audio.setSample (0, i, 0.5f * std::sin (0.05f * (float) i));
        data->sampleRate = kRate;
        data->path = "test-sine";
        return data;
    }

    int playLength (float tune, bool reverse, float start, float end)
    {
        auto p = plainVoice();
        p[vp::SrcMode] = 1.0f;
        p[vp::SmpTune] = tune;
        p[vp::SmpReverse] = reverse ? 1.0f : 0.0f;
        p[vp::SmpStart] = start;
        p[vp::SmpEnd] = end;

        Kit kit;
        kit.prepare (kRate, 1);
        kit.sampleSlot (0).setData (sine (4800));
        const auto params = kitWith (p);
        kit.setParameters (params);
        juce::AudioBuffer<float> buf (2, 1);
        kit.process (buf, {}); // picks up the sample
        kit.noteOn (0, 60, 1.0f);
        return samplesUntilIdle (kit, params, 48000);
    }

    void runTest() override
    {
        beginTest ("Tune changes speed: +12 st plays in half the time");
        expectWithinAbsoluteError (playLength (0.0f, false, 0.0f, 1.0f), 4800, 2);
        expectWithinAbsoluteError (playLength (12.0f, false, 0.0f, 1.0f), 2400, 2);
        expectWithinAbsoluteError (playLength (-12.0f, false, 0.0f, 1.0f), 9600, 2);

        beginTest ("Start/end and reverse");
        expectWithinAbsoluteError (playLength (0.0f, false, 0.25f, 0.75f), 2400, 2);
        expectWithinAbsoluteError (playLength (0.0f, true, 0.0f, 1.0f), 4800, 2);

        beginTest ("Gate + loop keeps playing while held");
        {
            auto p = plainVoice();
            p[vp::SrcMode] = 1.0f;
            p[vp::PlayMode] = 1.0f;
            p[vp::SmpLoop] = 1.0f;
            p[vp::SmpLoopStart] = 0.5f;
            Kit kit;
            kit.prepare (kRate, 1);
            kit.sampleSlot (0).setData (sine (4800));
            kit.setParameters (kitWith (p));
            juce::AudioBuffer<float> buf (2, 1);
            kit.process (buf, {});
            kit.noteOn (0, 60, 1.0f);
            expectEquals (samplesUntilIdle (kit, kitWith (p), 24000), 24000);
        }
    }
};

class FmTests final : public juce::UnitTest
{
public:
    FmTests() : juce::UnitTest ("FM", "Batida") {}

    void runTest() override
    {
        beginTest ("Every algorithm makes finite, audible sound");
        for (int a = 0; a < kNumAlgorithms; ++a)
        {
            auto p = plainVoice();
            p[vp::FmAlgo] = (float) a;
            p[vp::FmFeedback] = 1.0f;
            for (int o = 0; o < kNumOps; ++o)
                p[opParam (o, OpLevel)] = 0.7f;

            const auto audio = tools::renderVoice (kitWith (p), 0, 60, 1.0f, 0.5);
            const auto s = tools::measure (audio, kRate);
            expect (s.finite, "algorithm " + juce::String (a + 1) + " produced NaN/inf");
            expect (s.peakDb > -30.0f && s.peakDb < 15.0f,
                    "algorithm " + juce::String (a + 1) + " peak " + juce::String (s.peakDb) + " dB");
        }

        beginTest ("Macros act on modulators only");
        {
            auto p = plainVoice();
            p[vp::FmAlgo] = 3.0f; // 4 > 3, 2 > 1: carriers 1 and 3
            p[opParam (1, OpLevel)] = 0.4f;
            p[opParam (1, Ratio)] = 2.3f;
            p[opParam (1, OpD)] = 100.0f;
            p[vp::FmBright] = 1.0f;
            p[vp::FmHarm] = -1.0f;
            p[vp::FmBrightDecay] = 1.0f;
            p[vp::FmFeedback] = 0.8f;
            p[vp::FmGrit] = 0.5f;

            const auto fm = computeEffectiveFm (p);
            expectWithinAbsoluteError (fm.ops[1].level, 0.8f, 1.0e-4f);
            expectWithinAbsoluteError (fm.ops[1].ratio, 2.0f, 1.0e-4f);
            expectWithinAbsoluteError (fm.ops[1].decay, 25.0f, 1.0e-3f);
            expectWithinAbsoluteError (fm.ops[0].level, 1.0f, 1.0e-4f); // carrier untouched
            expectWithinAbsoluteError (fm.feedback, 1.0f, 1.0e-4f);      // clamped
        }

        beginTest ("Full feedback turns op 4 into noise");
        {
            auto p = plainVoice();
            p[vp::FmAlgo] = 7.0f; // all carriers
            p[opParam (0, OpLevel)] = 0.0f;
            p[opParam (3, OpLevel)] = 1.0f;
            p[vp::FmFeedback] = 1.0f;
            const auto noisy = tools::measure (tools::renderVoice (kitWith (p), 0, 60, 1.0f, 0.5), kRate);
            p[vp::FmFeedback] = 0.0f;
            const auto clean = tools::measure (tools::renderVoice (kitWith (p), 0, 60, 1.0f, 0.5), kRate);
            logMessage ("  brightness at feedback 0: " + juce::String (clean.centroidHz, 0)
                        + " Hz, at feedback 1: " + juce::String (noisy.centroidHz, 0) + " Hz");
            // Zero-crossing rate is a rough brightness measure; "much brighter" is the point.
            expectGreaterThan (noisy.centroidHz, clean.centroidHz * 5.0f);
        }
    }
};

class OversamplingTests final : public juce::UnitTest
{
public:
    OversamplingTests() : juce::UnitTest ("Oversampling and noise", "Batida") {}

    // Gain in dB of a sine at `hz` generated at 96 kHz and decimated to 48 kHz.
    static float decimatorGainDb (double hz)
    {
        HalfbandDecimator d;
        d.reset();
        float peak = 0.0f;
        for (int n = 0; n < 8000; ++n)
        {
            const auto a = (float) std::sin (2.0 * 3.141592653589793 * hz * (2 * n) / 96000.0);
            const auto b = (float) std::sin (2.0 * 3.141592653589793 * hz * (2 * n + 1) / 96000.0);
            const auto y = d.process (a, b);
            if (n > 200)
                peak = std::max (peak, std::abs (y));
        }
        return 20.0f * std::log10 (std::max (peak, 1.0e-9f));
    }

    void runTest() override
    {
        beginTest ("Halfband decimator: flat in the audio band, blocks what would alias");
        for (const auto hz : { 100.0, 5000.0, 12000.0, 18000.0 })
            expectWithinAbsoluteError (decimatorGainDb (hz), 0.0f, 0.2f, juce::String (hz) + " Hz");
        for (const auto hz : { 30000.0, 36000.0, 44000.0 })
            expectLessThan (decimatorGainDb (hz), -70.0f, juce::String (hz) + " Hz");
        logMessage ("  gain at 20 kHz: " + juce::String (decimatorGainDb (20000.0), 2) + " dB, at 28 kHz: "
                    + juce::String (decimatorGainDb (28000.0), 1) + " dB");

        beginTest ("Noise waveform is broadband and does not follow pitch");
        {
            auto p = plainVoice();
            p[vp::FmAlgo] = 7.0f;
            p[opParam (0, Wave)] = 1.0f;
            const auto low = tools::measure (tools::renderVoice (kitWith (p), 0, 36, 1.0f, 0.3), kRate);
            const auto high = tools::measure (tools::renderVoice (kitWith (p), 0, 84, 1.0f, 0.3), kRate);
            logMessage ("  zero-crossing rate: " + juce::String (low.centroidHz, 0) + " / "
                        + juce::String (high.centroidHz, 0) + " Hz");
            expectGreaterThan (low.centroidHz, 8000.0f);
            expectWithinAbsoluteError (high.centroidHz / low.centroidHz, 1.0f, 0.1f);
            expect (low.finite && low.peakDb > -12.0f && low.peakDb < 3.0f);
        }
    }
};

class DefaultKitTests final : public juce::UnitTest
{
public:
    DefaultKitTests() : juce::UnitTest ("Default kit", "Batida") {}

    void runTest() override
    {
        beginTest ("Every default sound is audible, finite and not clipping hard");
        const auto kit = defaultKitParams();
        for (int v = 0; v < kNumVoices; ++v)
        {
            const auto s = tools::measure (tools::renderVoice (kit, v, 60, 0.85f, 1.5, kRate, 0.25), kRate);
            const juce::String name = defaultVoiceName (v);
            expect (s.finite, name + " produced NaN/inf");
            expect (s.peakDb > -24.0f && s.peakDb < 6.0f, name + " peak " + juce::String (s.peakDb, 1) + " dB");
            expect (s.lengthMs > 20.0f, name + " is only " + juce::String (s.lengthMs, 0) + " ms");
        }

        beginTest ("Parameter table is complete and IDs are unique");
        {
            const auto& specs = voiceParamSpecs();
            expectEquals ((int) specs.size(), kNumVoiceParams);
            std::set<std::string> ids;
            for (int v = 0; v < kNumVoices; ++v)
                for (int p = 0; p < kNumVoiceParams; ++p)
                    ids.insert (voiceParamID (v, p));
            for (int g = 0; g < kNumGlobalParams; ++g)
                ids.insert (globalParamID (g));
            expectEquals ((int) ids.size(), kNumVoices * kNumVoiceParams + kNumGlobalParams);
        }
    }
};

class StackTests final : public juce::UnitTest
{
public:
    StackTests() : juce::UnitTest ("Stack", "Batida") {}

    struct Stereo { std::vector<float> l, r; };

    static Stereo play (const VoiceParams& p, double seconds, const SampleData* sample = nullptr)
    {
        Voice voice;
        voice.prepare (kRate);
        voice.setSampleData (sample);
        voice.setParameters (p);
        voice.noteOn (Voice::kBaseKey, 1.0f);
        const auto n = (int) (seconds * kRate);
        Stereo out { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
        voice.render (out.l.data(), out.r.data(), out.l.data(), out.r.data(), n);
        return out;
    }

    static float rmsDb (const Stereo& s, double from)
    {
        double sum = 0.0;
        const auto start = (size_t) (from * kRate);
        for (size_t i = start; i < s.l.size(); ++i)
            sum += 0.5 * (s.l[i] * s.l[i] + s.r[i] * s.r[i]);
        return 10.0f * std::log10 ((float) std::max (1.0e-12, sum / (double) (s.l.size() - start)));
    }

    // Level of one frequency in the mono sum (Goertzel), in dB.
    static float toneDb (const Stereo& s, double hz, double from)
    {
        const auto w = 6.283185307 * hz / kRate;
        const auto coeff = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        const auto start = (size_t) (from * kRate);
        for (size_t i = start; i < s.l.size(); ++i)
        {
            const auto s0 = 0.5 * (s.l[i] + s.r[i]) + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const auto power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        return 10.0f * (float) std::log10 (std::max (1.0e-12, power));
    }

    static float differenceDb (const Stereo& s)
    {
        double diff = 0.0, sum = 0.0;
        for (size_t i = 0; i < s.l.size(); ++i)
        {
            diff += (s.l[i] - s.r[i]) * (s.l[i] - s.r[i]);
            sum += (s.l[i] + s.r[i]) * (s.l[i] + s.r[i]);
        }
        return 10.0f * (float) std::log10 (std::max (1.0e-12, diff) / std::max (1.0e-12, sum));
    }

    static VoiceParams saw()
    {
        auto p = plainVoice();
        p[opParam (0, Wave)] = 0.5f;
        return p;
    }

    void runTest() override
    {
        beginTest ("One copy is the sound as before: mono, one pitch");
        {
            auto p = saw();
            p[vp::StackDetune] = 50.0f; // ignored with one copy
            p[vp::StackSpread] = 1.0f;
            const auto s = play (p, 0.5);
            expect (s.l == s.r, "left and right are the same");
        }

        beginTest ("Two to four copies keep the level of one");
        {
            const auto one = rmsDb (play (saw(), 1.0), 0.2);
            for (int copies = 2; copies <= kMaxStack; ++copies)
            {
                auto p = saw();
                p[vp::StackCount] = (float) (copies - 1);
                const auto s = play (p, 1.0);
                const auto level = rmsDb (s, 0.2);
                expect (std::isfinite (level));
                expectWithinAbsoluteError (level, one, 2.0f, juce::String (copies) + " copies");
            }
        }

        beginTest ("Spread makes it stereo; no spread keeps it mono");
        {
            auto p = saw();
            p[vp::StackCount] = 2.0f; // 3 copies
            p[vp::StackSpread] = 0.0f;
            expectLessThan (differenceDb (play (p, 0.5)), -100.0f);
            p[vp::StackSpread] = 1.0f;
            expectGreaterThan (differenceDb (play (p, 0.5)), -15.0f);
        }

        beginTest ("Detune spreads the copies around the pitch");
        {
            auto p = plainVoice(); // a sine at 261.6 Hz
            p[vp::StackCount] = 1.0f; // 2 copies
            p[vp::StackDetune] = 100.0f; // the two copies sit at the ends: a semitone down and up
            const auto s = play (p, 1.0);
            const auto base = (double) Voice::kBaseHz;
            expectGreaterThan (toneDb (s, base * std::exp2 (1.0 / 12.0), 0.1), toneDb (s, base, 0.1) + 10.0f);
            expectGreaterThan (toneDb (s, base * std::exp2 (-1.0 / 12.0), 0.1), toneDb (s, base, 0.1) + 10.0f);
        }

        beginTest ("Intervals: Minor 7 sounds root, minor third, fifth and seventh");
        {
            auto p = plainVoice();
            p[vp::StackCount] = 3.0f; // 4 copies
            p[vp::StackDetune] = 0.0f;
            p[vp::StackInterval] = (float) StackInterval::Minor7;
            const auto s = play (p, 1.0);
            const auto base = (double) Voice::kBaseHz;
            const auto root = toneDb (s, base, 0.1);
            for (auto semis : { 3, 7, 10 })
                expectWithinAbsoluteError (toneDb (s, base * std::exp2 (semis / 12.0), 0.1), root, 3.0f,
                                           "+" + juce::String (semis) + " st");
            expectLessThan (toneDb (s, base * std::exp2 (4.0 / 12.0), 0.1), root - 20.0f, "no major third");
        }

        beginTest ("Samples stack too: Octave adds the octave");
        {
            auto sample = SampleTests::sine (48000); // 382 Hz
            auto p = plainVoice();
            p[vp::SrcMode] = 1.0f;
            p[vp::StackCount] = 1.0f;
            p[vp::StackDetune] = 0.0f;
            p[vp::StackInterval] = (float) StackInterval::Octave;
            const auto hz = 0.05 * kRate / 6.283185307;
            const auto both = play (p, 0.4, sample.get());
            expectWithinAbsoluteError (toneDb (both, 2.0 * hz, 0.05), toneDb (both, hz, 0.05), 3.0f);
        }

        beginTest ("A slide keeps every copy sounding");
        {
            auto p = plainVoice(); // sines, so each copy is one tone
            p[vp::StackCount] = 1.0f;
            p[vp::StackDetune] = 0.0f;
            p[vp::StackInterval] = (float) StackInterval::Octave;
            p[vp::Glide] = 20.0f;
            p[vp::PlayMode] = 1.0f; // gate
            Voice voice;
            voice.prepare (kRate);
            voice.setParameters (p);
            voice.noteOn (Voice::kBaseKey, 1.0f);
            std::vector<float> l (24000), r (24000);
            voice.render (l.data(), r.data(), l.data(), r.data(), 12000);
            voice.noteOn (Voice::kBaseKey + 5, 1.0f);
            voice.render (l.data() + 12000, r.data() + 12000, l.data() + 12000, r.data() + 12000, 12000);
            const Stereo after { std::vector<float> (l.begin() + 14400, l.end()), std::vector<float> (r.begin() + 14400, r.end()) };
            const auto hz = (double) Voice::kBaseHz * std::exp2 (5.0 / 12.0);
            expectWithinAbsoluteError (toneDb (after, 2.0 * hz, 0.0), toneDb (after, hz, 0.0), 3.0f);
        }
    }
};

static EnvelopeTests envelopeTests;
static StackTests stackTests;
static VoiceTests voiceTests;
static MidiTests midiTests;
static SampleTests sampleTests;
static FmTests fmTests;
static OversamplingTests oversamplingTests;
static DefaultKitTests defaultKitTests;

} // namespace batida
