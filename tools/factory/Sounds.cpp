#include "Factory.h"

#include <cmath>
#include <set>

// The sound recipes. Each archetype is a starting point plus ranges; every
// candidate draws its values from the ranges with its own seed. FM Pitch is in
// semitones from C3 (261.6 Hz): -29 ≈ 49 Hz, -24 = C1 (65 Hz), +12 = 523 Hz,
// +24 = 1047 Hz. Choices are indices: fm_algo 0-7 = algorithms 1-8,
// drive_type 0-3 = Soft/Hard/Fold/Crush, flt_type 0-2 = LP/HP/BP,
// play_mode 1 = Gate, src_mode 1 = Sample, 2 = Layer. A range over a choice
// (e.g. 0.5..2.49) picks one of the choices it covers.

namespace batida::factory
{

namespace
{
// Engine-made samples ------------------------------------------------------------

juce::AudioBuffer<float> toBuffer (const std::vector<float>& x, int length = -1)
{
    const auto n = length < 0 ? (int) x.size() : std::min (length, (int) x.size());
    juce::AudioBuffer<float> b (1, std::max (1, n));
    b.clear();
    for (int i = 0; i < n; ++i)
        b.setSample (0, i, x[(size_t) i]);
    return b;
}

// Trims silence at the end, fades the last few ms, and normalises to -1 dBFS.
juce::AudioBuffer<float> finish (juce::AudioBuffer<float> b, bool trim = true, double fadeMs = 4.0)
{
    auto n = b.getNumSamples();
    const auto peak = b.getMagnitude (0, n);
    if (peak <= 0.0f)
        return b;
    if (trim)
    {
        int last = 0;
        for (int i = 0; i < n; ++i)
            if (std::abs (b.getSample (0, i)) > peak * 0.0005f)
                last = i;
        n = std::min (n, last + (int) (0.01 * kRate));
        b.setSize (1, n, true);
    }
    const auto fade = std::min (n, (int) (fadeMs * 0.001 * kRate));
    b.applyGainRamp (0, n - fade, fade, 1.0f, 0.0f);
    b.applyGain (0.891f / peak);
    return b;
}

// One hit of `p` through a kit chain set by `globals` (the voice in slot 1, the
// others silent), mono.
std::vector<float> throughChain (const VoiceParams& p, const Overrides& globals, double seconds)
{
    KitPreset kit;
    for (auto& v : kit.voices)
        v = voiceFrom ({ { "level", -60.0f } });
    kit.voices[0] = p;
    for (int g = 0; g < kNumGlobalParams; ++g)
        kit.globals[(size_t) g] = globalParamSpecs()[(size_t) g].def;
    applyGlobals (kit.globals, globals);
    Pattern pat;
    pat.length = 16;
    pat.tracks[0].steps[0].gate = true;
    pat.tracks[0].steps[0].velocity = 110;
    const auto bars = (int) std::ceil (seconds / 2.0);
    const auto audio = renderKit (kit, {}, pat, 120.0f, 0.5f, bars);
    std::vector<float> mono ((size_t) std::min (audio.getNumSamples(), (int) (seconds * kRate)));
    for (size_t i = 0; i < mono.size(); ++i)
        mono[i] = 0.5f * (audio.getSample (0, (int) i) + audio.getSample (1, (int) i));
    return mono;
}

float between (juce::Random& r, float lo, float hi) { return lo + (hi - lo) * r.nextFloat(); }
float logBetween (juce::Random& r, float lo, float hi) { return lo * std::pow (hi / lo, r.nextFloat()); }

// A clap: a few short noise bursts, then a tail.
juce::AudioBuffer<float> makeClap (uint32_t seed)
{
    juce::Random r ((juce::int64) seed);
    const auto centre = logBetween (r, 900.0f, 1800.0f);
    const auto bursts = 3 + r.nextInt (3);
    std::vector<float> out ((size_t) (0.7 * kRate), 0.0f);
    int at = 0;
    for (int b = 0; b <= bursts; ++b)
    {
        const auto last = b == bursts;
        const auto p = voiceFrom ({ { "fm_algo", 7 }, { "op1_wave", 1.0f }, { "op1_level", 1.0f },
                                    { "amp_decay", last ? logBetween (r, 140.0f, 320.0f) : between (r, 6.0f, 12.0f) },
                                    { "amp_release", 20.0f }, { "flt_type", 2 },
                                    { "flt_cutoff", centre * between (r, 0.9f, 1.1f) }, { "flt_res", 0.3f } });
        const auto hit = renderHit (p, nullptr, 0.5);
        const auto gain = last ? 1.0f : between (r, 0.6f, 1.0f);
        for (size_t i = 0; i < hit.size() && at + (int) i < (int) out.size(); ++i)
            out[(size_t) at + i] += gain * hit[i];
        at += (int) (between (r, 7.0f, 14.0f) * 0.001f * kRate);
    }
    return finish (toBuffer (out));
}

// A drum hit resampled through a warm, hot chain: dusty, for Sample sounds.
juce::AudioBuffer<float> makeResampled (const Overrides& hit, uint32_t seed, double seconds)
{
    juce::Random r ((juce::int64) seed);
    auto p = voiceFrom (hit);
    p[vp::FmPitch] += between (r, -2.0f, 2.0f);
    const auto mono = throughChain (p, { { "xy_x", between (r, 0.05f, 0.35f) }, { "xy_y", between (r, 0.45f, 0.8f) },
                                         { "eq_lp", logBetween (r, 4500.0f, 9000.0f) }, { "eq_low", between (r, 0.0f, 3.0f) } },
                                    seconds);
    return finish (toBuffer (mono));
}

const Overrides kSnareHit { { "fm_pitch", -6.0f }, { "fm_algo", 7 },
                            { "op1_level", 0.6f }, { "op1_decay", 140.0f }, { "op1_sustain", 0.0f }, { "op1_release", 40.0f },
                            { "op2_ratio", 1.6f }, { "op2_level", 0.3f }, { "op2_decay", 90.0f }, { "op2_sustain", 0.0f }, { "op2_release", 30.0f },
                            { "op3_wave", 1.0f }, { "op3_level", 0.9f }, { "op3_decay", 260.0f }, { "op3_sustain", 0.0f }, { "op3_release", 60.0f },
                            { "pitch_amt", 7.0f }, { "pitch_decay", 40.0f }, { "amp_decay", 300.0f }, { "punch", 0.4f } };

const Overrides kKickHit { { "fm_pitch", -27.0f }, { "fm_algo", 3 },
                           { "op2_ratio", 2.0f }, { "op2_level", 0.15f }, { "op2_decay", 20.0f }, { "op2_sustain", 0.0f }, { "op2_release", 20.0f },
                           { "op3_wave", 1.0f }, { "op3_level", 0.08f }, { "op3_decay", 15.0f }, { "op3_sustain", 0.0f }, { "op3_release", 10.0f },
                           { "pitch_amt", 20.0f }, { "pitch_decay", 60.0f }, { "amp_decay", 380.0f }, { "punch", 0.35f } };

// A crash or a snare, rendered long through the chain, to be played reversed.
juce::AudioBuffer<float> makeReverse (uint32_t seed)
{
    juce::Random r ((juce::int64) seed);
    VoiceParams p;
    if (r.nextBool())
        p = voiceFrom ({ { "fm_algo", 5 }, { "op1_wave", 1.0f }, { "op1_level", 0.8f },
                         { "op2_fixed", 1 }, { "op2_freq", between (r, 400.0f, 700.0f) }, { "op2_wave", 0.75f }, { "op2_level", 0.15f },
                         { "op3_ratio", 1.0f }, { "op3_level", 0.4f }, { "op4_ratio", between (r, 2.3f, 3.7f) }, { "op4_level", 0.6f },
                         { "fm_pitch", between (r, 18.0f, 28.0f) }, { "fm_harm", 0.6f },
                         { "amp_decay", logBetween (r, 1200.0f, 2200.0f) }, { "flt_type", 1 }, { "flt_cutoff", logBetween (r, 1500.0f, 4000.0f) } });
    else
    {
        p = voiceFrom (kSnareHit);
        p[vp::AmpD] = logBetween (r, 500.0f, 1100.0f);
        p[opParam (2, OpD)] = p[vp::AmpD];
    }
    auto mono = throughChain (p, { { "xy_x", between (r, 0.2f, 0.8f) }, { "xy_y", between (r, 0.3f, 0.7f) } }, 2.0);
    auto b = finish (toBuffer (mono), true, 2.0);
    return b;
}

// A short FM hit chopped into repeats that step in pitch: a stutter.
juce::AudioBuffer<float> makeStutter (uint32_t seed)
{
    juce::Random r ((juce::int64) seed);
    const auto p = voiceFrom ({ { "fm_pitch", between (r, -6.0f, 18.0f) }, { "fm_algo", r.nextInt (4) },
                                { "op2_ratio", (float) (1 + r.nextInt (4)) }, { "op2_level", between (r, 0.3f, 0.8f) },
                                { "op2_decay", logBetween (r, 20.0f, 120.0f) }, { "op2_sustain", 0.0f },
                                { "fm_feedback", between (r, 0.0f, 0.6f) }, { "pitch_amt", between (r, 0.0f, 24.0f) },
                                { "pitch_decay", logBetween (r, 10.0f, 60.0f) },
                                { "amp_decay", 300.0f }, { "drive", between (r, 0.2f, 0.7f) }, { "drive_type", 3 } });
    const auto repeats = 4 + r.nextInt (6);
    const auto segment = (int) (logBetween (r, 18.0f, 60.0f) * 0.001f * kRate);
    const auto stepSt = r.nextBool() ? (r.nextBool() ? 1 : -1) * (1 + r.nextInt (3)) : 0;
    std::vector<float> out ((size_t) (segment * repeats + kRate * 0.3), 0.0f);
    for (int k = 0; k < repeats; ++k)
    {
        const auto hit = renderHit (p, nullptr, 0.4, 0.25, Voice::kBaseKey + k * stepSt);
        const auto len = k == repeats - 1 ? (int) hit.size() : segment;
        const auto gain = std::pow (0.88f, (float) k);
        for (int i = 0; i < len && i < (int) hit.size(); ++i)
        {
            const auto edge = std::min (1.0f, std::min ((float) i, (float) (len - i)) / 48.0f); // no clicks at the cuts
            out[(size_t) (k * segment + i)] += gain * edge * hit[(size_t) i];
        }
    }
    return finish (toBuffer (out));
}

// One bar of a small engine kit at 120 bpm through the chain: a loop to slice.
juce::AudioBuffer<float> makeLoop (uint32_t seed)
{
    juce::Random r ((juce::int64) seed);
    KitPreset kit;
    for (int g = 0; g < kNumGlobalParams; ++g)
        kit.globals[(size_t) g] = globalParamSpecs()[(size_t) g].def;
    applyGlobals (kit.globals, { { "xy_x", between (r, 0.0f, 1.0f) }, { "xy_y", between (r, 0.2f, 0.8f) },
                                 { "comp_amount", between (r, 0.2f, 0.6f) } });
    const auto base = defaultKitParams();
    for (int v = 0; v < kNumVoices; ++v)
    {
        kit.voices[(size_t) v] = base.voices[(size_t) v];
        kit.voices[(size_t) v][vp::FmPitch] += between (r, -3.0f, 3.0f);
        kit.voices[(size_t) v][vp::AmpD] *= between (r, 0.6f, 1.3f);
    }
    kit.voices[5][vp::Level] = -60.0f; // no bass
    Pattern pat;
    pat.length = 16;
    auto hit = [&] (int track, int step, int vel) { auto& s = pat.tracks[(size_t) track].steps[(size_t) step]; s.gate = true; s.velocity = (uint8_t) vel; };
    hit (0, 0, 120);
    for (int s = 1; s < 16; ++s)
    {
        if (r.nextFloat() < (s % 4 == 0 ? 0.35f : 0.12f)) hit (0, s, 90 + r.nextInt (30));
        if ((s == 4 || s == 12) || r.nextFloat() < 0.1f) hit (r.nextBool() ? 2 : 3, s, s % 4 == 0 ? 118 : 50 + r.nextInt (40));
        if (s % 2 == 0 || r.nextFloat() < 0.4f) hit (r.nextFloat() < 0.15f ? 7 : 6, s, 60 + r.nextInt (50));
        if (r.nextFloat() < 0.12f) hit (r.nextBool() ? 1 : 4, s, 70 + r.nextInt (40));
    }
    hit (6, 0, 90);
    const auto audio = renderKit (kit, {}, pat, 120.0f, 0.5f, 1);
    std::vector<float> mono ((size_t) (2.0 * kRate));
    for (size_t i = 0; i < mono.size(); ++i)
        mono[i] = 0.5f * (audio.getSample (0, (int) i) + audio.getSample (1, (int) i));
    return finish (toBuffer (mono), false, 3.0);
}

// Vinyl-like crackle from engine clicks, over a faint hiss.
juce::AudioBuffer<float> makeCrackle (uint32_t seed)
{
    juce::Random r ((juce::int64) seed);
    const auto length = (int) (2.5 * kRate);
    std::vector<float> out ((size_t) length, 0.0f);

    std::vector<std::vector<float>> clicks;
    for (int k = 0; k < 4; ++k)
    {
        const auto p = voiceFrom ({ { "fm_algo", 7 }, { "op1_wave", 1.0f }, { "op1_level", 1.0f },
                                    { "amp_decay", between (r, 5.0f, 9.0f) }, { "amp_release", 5.0f },
                                    { "flt_type", 2 }, { "flt_cutoff", logBetween (r, 1200.0f, 6000.0f) }, { "flt_res", 0.2f } });
        auto c = renderHit (p, nullptr, 0.03);
        clicks.push_back (std::move (c));
    }
    const auto rate = between (r, 12.0f, 40.0f); // clicks per second
    for (int i = 0; i < length;)
    {
        i += (int) (-std::log (std::max (1.0e-4f, r.nextFloat())) / rate * kRate);
        if (i >= length)
            break;
        const auto& c = clicks[(size_t) r.nextInt ((int) clicks.size())];
        const auto gain = std::pow (10.0f, between (r, -30.0f, 0.0f) / 20.0f);
        for (size_t k = 0; k < c.size() && i + (int) k < length; ++k)
            out[(size_t) i + k] += gain * c[k];
    }
    const auto hissVoice = voiceFrom ({ { "play_mode", 1 }, { "fm_algo", 7 }, { "op1_wave", 1.0f }, { "op1_level", 1.0f },
                                        { "amp_sustain", 1.0f }, { "amp_release", 20.0f },
                                        { "flt_cutoff", logBetween (r, 2500.0f, 6000.0f) } });
    const auto hiss = renderHit (hissVoice, nullptr, 2.6, 2.6);
    const auto hissGain = std::pow (10.0f, between (r, -40.0f, -30.0f) / 20.0f);
    for (int i = 0; i < length; ++i)
        out[(size_t) i] += hissGain * hiss[(size_t) i];
    auto b = finish (toBuffer (out), false, 20.0);
    b.applyGainRamp (0, 0, (int) (0.02 * kRate), 0.0f, 1.0f);
    return b;
}

// A crushed noise transient, for layering over an FM body.
juce::AudioBuffer<float> makeTransient (uint32_t seed)
{
    juce::Random r ((juce::int64) seed);
    const auto p = voiceFrom ({ { "fm_algo", 7 }, { "op1_wave", 1.0f }, { "op1_level", 1.0f },
                                { "op2_ratio", 1.0f }, { "op2_fixed", 1 }, { "op2_freq", logBetween (r, 800.0f, 3000.0f) }, { "op2_level", 0.4f },
                                { "op2_decay", 10.0f }, { "op2_sustain", 0.0f },
                                { "amp_decay", logBetween (r, 12.0f, 40.0f) }, { "amp_release", 10.0f },
                                { "drive", between (r, 0.3f, 0.9f) }, { "drive_type", 3 },
                                { "flt_type", 2 }, { "flt_cutoff", logBetween (r, 1500.0f, 7000.0f) }, { "flt_res", 0.3f } });
    return finish (toBuffer (renderHit (p, nullptr, 0.15)));
}

// The recipes ---------------------------------------------------------------------

// Plays a sample as it is: the amp envelope stays open for its length.
const Overrides kSamplePlays { { "src_mode", 1 }, { "amp_decay", 8000.0f }, { "amp_release", 60.0f } };

Overrides join (Overrides a, const Overrides& b)
{
    a.insert (a.end(), b.begin(), b.end());
    return a;
}

std::vector<Archetype> build()
{
    std::vector<Archetype> a;
    auto add = [&] (Archetype x) { a.push_back (std::move (x)); };

    // Kicks --------------------------------------------------------------------
    add ({ "kick.drop", "kick", { "Kick", "Drop" },
           { { "fm_algo", 3 }, { "op2_ratio", 2.0f }, { "op2_decay", 20.0f }, { "op2_sustain", 0.0f }, { "op2_release", 20.0f },
             { "amp_release", 50.0f }, { "punch", 0.25f } },
           { { "fm_pitch", -31.0f, -26.0f }, { "pitch_amt", 18.0f, 34.0f }, { "pitch_decay", 60.0f, 220.0f, true },
             { "amp_decay", 300.0f, 700.0f, true }, { "op2_level", 0.05f, 0.25f }, { "drive", 0.0f, 0.15f } },
           14, 4, { "warm" } });
    add ({ "kick.click", "kick", { "Kick", "Click" },
           { { "fm_algo", 3 }, { "op2_sustain", 0.0f }, { "op2_release", 8.0f }, { "amp_release", 30.0f }, { "punch", 0.3f } },
           { { "fm_pitch", -28.0f, -24.0f }, { "pitch_amt", 24.0f, 40.0f }, { "pitch_decay", 12.0f, 40.0f, true },
             { "amp_decay", 150.0f, 320.0f, true }, { "op2_ratio", 3.0f, 7.0f }, { "op2_level", 0.08f, 0.22f }, { "op2_decay", 4.0f, 14.0f, true } },
           14, 4, { "digital" } });
    add ({ "kick.boom", "kick", { "Boom", "Kick" },
           { { "fm_algo", 3 }, { "op2_ratio", 1.0f }, { "op2_decay", 20.0f }, { "op2_sustain", 0.0f }, { "amp_release", 80.0f }, { "punch", 0.2f } },
           { { "fm_pitch", -33.0f, -29.0f }, { "pitch_amt", 10.0f, 22.0f }, { "pitch_decay", 50.0f, 140.0f, true },
             { "amp_decay", 900.0f, 1800.0f, true }, { "op2_level", 0.0f, 0.1f }, { "drive", 0.05f, 0.25f } },
           11, 3, { "warm" } });
    add ({ "kick.dist", "kick", { "Kick", "Hammer" },
           { { "fm_algo", 3 }, { "op2_ratio", 1.5f }, { "op2_level", 0.15f }, { "op2_decay", 30.0f }, { "op2_sustain", 0.0f },
             { "amp_release", 40.0f } },
           { { "fm_pitch", -30.0f, -26.0f }, { "pitch_amt", 20.0f, 34.0f }, { "pitch_decay", 40.0f, 120.0f, true },
             { "amp_decay", 300.0f, 600.0f, true }, { "drive", 0.45f, 0.85f }, { "drive_type", 0.5f, 2.49f },
             { "punch", 0.2f, 0.5f }, { "flt_cutoff", 3000.0f, 9000.0f, true } },
           14, 4, { "harsh" } });
    add ({ "kick.sub", "kick", { "Sub Kick" },
           { { "fm_algo", 3 }, { "amp_release", 80.0f }, { "punch", 0.15f } },
           { { "fm_pitch", -34.0f, -30.0f }, { "pitch_amt", 5.0f, 12.0f }, { "pitch_decay", 30.0f, 90.0f, true },
             { "amp_decay", 600.0f, 1200.0f, true } },
           10, 3, { "warm" } });
    add ({ "kick.tight", "kick", { "Kick", "Knock" },
           { { "fm_algo", 3 }, { "op2_ratio", 3.0f }, { "op2_sustain", 0.0f }, { "op2_release", 10.0f }, { "amp_release", 25.0f } },
           { { "fm_pitch", -28.0f, -23.0f }, { "pitch_amt", 20.0f, 30.0f }, { "pitch_decay", 15.0f, 45.0f, true },
             { "amp_decay", 90.0f, 180.0f, true }, { "punch", 0.2f, 0.45f }, { "op2_level", 0.05f, 0.2f }, { "op2_decay", 6.0f, 12.0f } },
           14, 4, {} });
    add ({ "kick.dusty", "kick", { "Kick" },
           join (kSamplePlays, { { "flt_res", 0.1f } }),
           { { "smp_tune", -4.0f, 0.0f }, { "flt_cutoff", 2500.0f, 7000.0f, true } },
           11, 3, { "warm", "organic" },
           [] (uint32_t seed) { return makeResampled (kKickHit, seed, 0.6); } });
    add ({ "kick.house", "kick", { "Kick", "Thump" },
           { { "fm_algo", 3 }, { "op2_sustain", 0.0f }, { "op2_release", 15.0f },
             { "op3_wave", 1.0f }, { "op3_sustain", 0.0f }, { "op3_release", 8.0f }, { "amp_release", 50.0f } },
           { { "fm_pitch", -29.0f, -26.0f }, { "pitch_amt", 14.0f, 24.0f }, { "pitch_decay", 30.0f, 70.0f, true },
             { "amp_decay", 350.0f, 650.0f, true }, { "op2_ratio", 2.0f, 4.0f }, { "op2_level", 0.15f, 0.28f },
             { "op2_decay", 10.0f, 25.0f }, { "op3_level", 0.03f, 0.08f }, { "op3_decay", 8.0f, 15.0f },
             { "punch", 0.3f, 0.5f }, { "drive", 0.05f, 0.2f } },
           11, 3, { "warm" } });

    // Snares and claps ---------------------------------------------------------
    const Overrides snareBody { { "fm_algo", 7 }, { "op1_sustain", 0.0f }, { "op1_release", 40.0f },
                                { "op2_sustain", 0.0f }, { "op2_release", 30.0f },
                                { "op3_wave", 1.0f }, { "op3_sustain", 0.0f }, { "op3_release", 60.0f }, { "amp_release", 60.0f } };
    add ({ "snare.body", "snare", { "Snare" }, snareBody,
           { { "fm_pitch", -9.0f, -3.0f }, { "op1_level", 0.5f, 0.7f }, { "op1_decay", 100.0f, 180.0f, true },
             { "op2_ratio", 1.4f, 1.8f }, { "op2_level", 0.2f, 0.35f }, { "op2_decay", 60.0f, 110.0f, true },
             { "op3_level", 0.7f, 1.0f }, { "op3_decay", 180.0f, 320.0f, true }, { "pitch_amt", 5.0f, 9.0f },
             { "pitch_decay", 30.0f, 50.0f }, { "amp_decay", 220.0f, 360.0f, true }, { "flt_cutoff", 8000.0f, 14000.0f, true },
             { "punch", 0.3f, 0.45f }, { "drive", 0.05f, 0.2f } },
           14, 4, { "organic" } });
    add ({ "snare.rim", "snare", { "Rim" },
           { { "fm_algo", 7 }, { "op1_sustain", 0.0f }, { "op1_release", 20.0f }, { "op2_sustain", 0.0f }, { "op2_release", 20.0f },
             { "op3_wave", 1.0f }, { "op3_sustain", 0.0f }, { "op3_release", 10.0f }, { "amp_release", 30.0f }, { "flt_type", 1 },
             { "flt_res", 0.1f } },
           { { "fm_pitch", 8.0f, 14.0f }, { "op1_level", 0.8f, 1.0f }, { "op1_decay", 30.0f, 60.0f },
             { "op2_ratio", 2.9f, 3.9f }, { "op2_level", 0.6f, 0.9f }, { "op2_decay", 25.0f, 45.0f },
             { "op3_level", 0.4f, 0.7f }, { "op3_decay", 8.0f, 16.0f }, { "amp_decay", 50.0f, 90.0f },
             { "flt_cutoff", 250.0f, 450.0f, true } },
           11, 3, { "organic" } });
    add ({ "snare.clap", "snare", { "Clap" },
           { { "fm_algo", 7 }, { "op1_wave", 1.0f }, { "op1_level", 1.0f }, { "op2_wave", 1.0f }, { "op2_level", 1.0f },
             { "op3_wave", 1.0f }, { "op3_level", 1.0f }, { "flt_type", 2 }, { "punch", 0.2f } },
           { { "amp_decay", 15.0f, 30.0f }, { "amp_sustain", 0.3f, 0.5f }, { "amp_release", 150.0f, 350.0f, true },
             { "flt_cutoff", 900.0f, 1600.0f, true }, { "flt_res", 0.2f, 0.45f } },
           11, 3, { "organic" } });
    add ({ "snare.clapstack", "snare", { "Clap", "Hands" }, kSamplePlays,
           { { "smp_tune", -1.5f, 1.5f } },
           12, 4, { "organic" }, [] (uint32_t seed) { return makeClap (seed); } });
    add ({ "snare.snap", "snare", { "Snap" },
           { { "fm_algo", 7 }, { "op1_wave", 1.0f }, { "op1_level", 1.0f }, { "op4_wave", 1.0f }, { "op4_level", 1.0f },
             { "op2_fixed", 1 }, { "op2_level", 0.15f },
             { "op2_decay", 15.0f }, { "op2_sustain", 0.0f }, { "amp_release", 20.0f }, { "flt_type", 1 }, { "punch", 0.1f } },
           { { "op2_freq", 1500.0f, 2500.0f, true }, { "amp_decay", 35.0f, 90.0f, true }, { "flt_cutoff", 1500.0f, 3500.0f, true },
             { "flt_res", 0.1f, 0.3f } },
           11, 3, {} });
    add ({ "snare.metal", "snare", { "Snare" },
           { { "fm_algo", 4 }, { "op1_ratio", 1.0f }, { "op2_ratio", 1.47f }, { "op3_ratio", 2.09f },
             { "op1_sustain", 0.0f }, { "op2_sustain", 0.0f }, { "op3_sustain", 0.0f }, { "op4_sustain", 0.0f },
             { "op2_level", 0.5f }, { "op3_level", 0.5f }, { "op1_level", 0.6f }, { "amp_release", 40.0f } },
           { { "fm_pitch", -2.0f, 6.0f }, { "fm_harm", 0.3f, 0.8f }, { "fm_feedback", 0.3f, 0.6f },
             { "op1_decay", 80.0f, 200.0f, true }, { "op2_decay", 80.0f, 200.0f, true }, { "op3_decay", 80.0f, 200.0f, true },
             { "op4_ratio", 3.1f, 5.3f }, { "op4_level", 0.3f, 0.6f }, { "op4_decay", 40.0f, 100.0f, true },
             { "amp_decay", 150.0f, 300.0f, true } },
           12, 3, { "metallic" } });
    add ({ "snare.dusty", "snare", { "Snare" }, join (kSamplePlays, { { "flt_res", 0.1f } }),
           { { "smp_tune", -4.0f, -1.0f }, { "flt_cutoff", 3000.0f, 8000.0f, true } },
           11, 3, { "warm", "organic" },
           [] (uint32_t seed) { return makeResampled (kSnareHit, seed, 0.7); } });
    add ({ "snare.fat", "snare", { "Snare" }, join (snareBody, { { "drive_type", 0 } }),
           { { "fm_pitch", -12.0f, -7.0f }, { "op1_level", 0.6f, 0.8f }, { "op1_decay", 140.0f, 220.0f, true },
             { "op2_ratio", 1.4f, 1.7f }, { "op2_level", 0.2f, 0.3f }, { "op2_decay", 80.0f, 140.0f, true },
             { "op3_level", 0.7f, 0.9f }, { "op3_decay", 250.0f, 420.0f, true }, { "pitch_amt", 4.0f, 8.0f },
             { "pitch_decay", 30.0f, 60.0f }, { "amp_decay", 300.0f, 450.0f, true }, { "flt_cutoff", 6000.0f, 9000.0f, true },
             { "punch", 0.35f, 0.5f }, { "drive", 0.15f, 0.3f } },
           11, 3, { "warm" } });
    add ({ "snare.crack", "snare", { "Snare", "Crack" }, join (snareBody, { { "drive_type", 1 }, { "flt_type", 1 } }),
           { { "fm_pitch", 1.0f, 7.0f }, { "op1_level", 0.4f, 0.6f }, { "op1_decay", 60.0f, 120.0f, true },
             { "op2_ratio", 1.5f, 2.2f }, { "op2_level", 0.2f, 0.4f }, { "op2_decay", 40.0f, 90.0f, true },
             { "op3_level", 0.8f, 1.0f }, { "op3_decay", 150.0f, 260.0f, true }, { "pitch_amt", 6.0f, 12.0f },
             { "pitch_decay", 20.0f, 40.0f }, { "amp_decay", 150.0f, 260.0f, true }, { "flt_cutoff", 400.0f, 900.0f, true },
             { "punch", 0.5f, 0.7f }, { "drive", 0.2f, 0.4f } },
           9, 2, { "harsh" } });

    // Hats and cymbals ---------------------------------------------------------
    const Overrides hatBase { { "fm_algo", 7 }, { "op1_wave", 1.0f }, { "op1_level", 1.0f }, { "op4_wave", 1.0f }, { "op4_level", 1.0f },
                              { "op2_fixed", 1 }, { "op2_wave", 0.75f }, { "op3_fixed", 1 }, { "op3_wave", 0.75f },
                              { "flt_type", 2 } };
    add ({ "hat.closed", "hat", { "Hat" }, join (hatBase, { { "amp_release", 25.0f } }),
           { { "op2_freq", 480.0f, 620.0f }, { "op3_freq", 740.0f, 900.0f }, { "op2_level", 0.05f, 0.15f }, { "op3_level", 0.05f, 0.15f },
             { "amp_decay", 35.0f, 80.0f, true }, { "flt_cutoff", 7000.0f, 11000.0f, true }, { "flt_res", 0.1f, 0.25f } },
           17, 5, { "metallic" } });
    add ({ "hat.open", "hat", { "Open Hat" }, join (hatBase, { { "amp_release", 60.0f } }),
           { { "op2_freq", 480.0f, 620.0f }, { "op3_freq", 740.0f, 900.0f }, { "op2_level", 0.05f, 0.15f }, { "op3_level", 0.05f, 0.15f },
             { "amp_decay", 280.0f, 700.0f, true }, { "flt_cutoff", 7000.0f, 11000.0f, true }, { "flt_res", 0.1f, 0.25f } },
           14, 4, { "metallic" } });
    add ({ "hat.ride", "hat", { "Ride" },
           { { "fm_algo", 4 }, { "op1_ratio", 1.0f }, { "op1_level", 0.5f }, { "op2_level", 0.5f }, { "op3_level", 0.5f },
             { "op1_sustain", 0.0f }, { "op2_sustain", 0.0f }, { "op3_sustain", 0.0f }, { "op4_sustain", 0.0f },
             { "flt_type", 1 }, { "amp_release", 200.0f } },
           { { "fm_pitch", 22.0f, 30.0f }, { "fm_harm", 0.2f, 0.6f }, { "op2_ratio", 1.34f, 1.52f }, { "op3_ratio", 2.1f, 2.7f },
             { "op4_ratio", 2.4f, 3.6f }, { "op4_level", 0.35f, 0.6f }, { "op1_decay", 600.0f, 1400.0f, true },
             { "op2_decay", 600.0f, 1400.0f, true }, { "op3_decay", 600.0f, 1400.0f, true }, { "op4_decay", 300.0f, 900.0f, true },
             { "amp_decay", 900.0f, 1600.0f, true }, { "flt_cutoff", 2000.0f, 4000.0f, true } },
           11, 3, { "metallic" } });
    add ({ "hat.crash", "hat", { "Crash" },
           { { "fm_algo", 5 }, { "op1_wave", 1.0f }, { "op1_level", 0.8f }, { "op2_fixed", 1 }, { "op2_wave", 0.75f },
             { "op2_level", 0.12f }, { "op3_level", 0.4f }, { "op4_level", 0.6f }, { "fm_harm", 0.6f },
             { "op1_sustain", 0.0f }, { "op2_sustain", 0.0f }, { "op3_sustain", 0.0f }, { "flt_type", 1 }, { "amp_release", 300.0f } },
           { { "fm_pitch", 18.0f, 28.0f }, { "op2_freq", 400.0f, 700.0f, true }, { "op4_ratio", 2.3f, 3.7f },
             { "op1_decay", 1200.0f, 2500.0f, true }, { "op2_decay", 600.0f, 1500.0f, true }, { "op3_decay", 800.0f, 2000.0f, true },
             { "amp_decay", 1200.0f, 2500.0f, true }, { "flt_cutoff", 2500.0f, 5000.0f, true } },
           11, 3, { "metallic" } });
    add ({ "hat.shaker", "hat", { "Shaker" },
           { { "fm_algo", 7 }, { "op1_wave", 1.0f }, { "op1_level", 1.0f }, { "flt_type", 1 }, { "amp_release", 30.0f } },
           { { "amp_attack", 6.0f, 20.0f }, { "amp_decay", 50.0f, 110.0f, true }, { "flt_cutoff", 4000.0f, 7000.0f, true },
             { "flt_res", 0.0f, 0.2f } },
           11, 3, { "organic" } });
    add ({ "hat.metal", "hat", { "Hat" },
           { { "fm_algo", 4 }, { "op1_level", 0.6f }, { "op2_level", 0.5f }, { "op2_ratio", 1.41f }, { "op3_level", 0.4f },
             { "op3_ratio", 2.23f }, { "flt_type", 1 }, { "amp_release", 30.0f } },
           { { "fm_pitch", 24.0f, 36.0f }, { "op4_ratio", 1.4f, 3.3f }, { "op4_level", 0.5f, 0.9f }, { "fm_harm", 0.5f, 1.0f },
             { "fm_feedback", 0.2f, 0.5f }, { "amp_decay", 30.0f, 120.0f, true }, { "flt_cutoff", 5000.0f, 8000.0f, true } },
           12, 3, { "metallic" } });
    add ({ "hat.bit", "hat", { "Hat" }, join (hatBase, { { "drive_type", 3 }, { "amp_release", 25.0f } }),
           { { "op2_freq", 480.0f, 620.0f }, { "op3_freq", 740.0f, 900.0f }, { "op2_level", 0.05f, 0.2f }, { "op3_level", 0.05f, 0.2f },
             { "drive", 0.4f, 0.9f }, { "amp_decay", 30.0f, 150.0f, true }, { "flt_cutoff", 6000.0f, 10000.0f, true },
             { "flt_res", 0.1f, 0.3f } },
           12, 3, { "digital" } });
    add ({ "hat.tick", "hat", { "Tick" },
           { { "fm_algo", 7 }, { "op1_wave", 1.0f }, { "op1_level", 1.0f }, { "op2_fixed", 1 }, { "op2_level", 0.1f },
             { "op2_decay", 10.0f }, { "op2_sustain", 0.0f }, { "flt_type", 1 }, { "flt_res", 0.2f }, { "amp_release", 10.0f } },
           { { "op2_freq", 6000.0f, 9000.0f, true }, { "amp_decay", 12.0f, 30.0f, true }, { "flt_cutoff", 7000.0f, 11000.0f, true } },
           9, 2, { "digital" } });

    // Percussion -------------------------------------------------------------------
    add ({ "perc.tom", "perc", { "Tom" },
           { { "fm_algo", 3 }, { "op2_ratio", 1.5f }, { "op2_decay", 60.0f }, { "op2_sustain", 0.0f }, { "op2_release", 40.0f },
             { "amp_release", 60.0f }, { "punch", 0.15f } },
           { { "fm_pitch", -22.0f, -4.0f }, { "pitch_amt", 6.0f, 14.0f }, { "pitch_decay", 150.0f, 300.0f, true },
             { "amp_decay", 300.0f, 700.0f, true }, { "op2_level", 0.05f, 0.2f } },
           14, 4, { "organic" } });
    add ({ "perc.conga", "perc", { "Conga", "Bongo" },
           { { "fm_algo", 3 }, { "op2_ratio", 1.5f }, { "op2_decay", 20.0f }, { "op2_sustain", 0.0f }, { "op2_release", 20.0f },
             { "amp_release", 40.0f }, { "punch", 0.3f } },
           { { "fm_pitch", -6.0f, 8.0f }, { "pitch_amt", 3.0f, 7.0f }, { "pitch_decay", 20.0f, 50.0f },
             { "amp_decay", 150.0f, 320.0f, true }, { "op2_level", 0.08f, 0.2f } },
           11, 3, { "organic" } });
    add ({ "perc.block", "perc", { "Block" },
           { { "fm_algo", 3 }, { "op2_ratio", 2.7f }, { "op2_decay", 15.0f }, { "op2_sustain", 0.0f }, { "amp_release", 15.0f } },
           { { "fm_pitch", 12.0f, 26.0f }, { "amp_decay", 25.0f, 70.0f, true }, { "op2_level", 0.15f, 0.3f } },
           11, 3, { "organic" } });
    add ({ "perc.cowbell", "perc", { "Bell" },
           { { "fm_algo", 7 }, { "fm_pitch", 12.0f }, { "op1_ratio", 1.0f }, { "op1_wave", 0.75f }, { "op1_level", 0.5f },
             { "op2_wave", 0.75f }, { "op2_level", 0.5f }, { "flt_type", 2 }, { "flt_res", 0.3f },
             { "amp_decay", 18.0f } },
           { { "op2_ratio", 1.45f, 1.52f }, { "flt_cutoff", 900.0f, 1600.0f, true },
             { "amp_sustain", 0.35f, 0.5f }, { "amp_release", 150.0f, 300.0f, true } },
           11, 3, { "metallic" } });
    add ({ "perc.zap", "perc", { "Zap" },
           { { "fm_algo", 3 }, { "op2_sustain", 0.0f }, { "op2_decay", 60.0f }, { "amp_release", 30.0f } },
           { { "fm_pitch", -2.0f, 14.0f }, { "pitch_amt", 30.0f, 48.0f }, { "pitch_decay", 20.0f, 80.0f, true },
             { "amp_decay", 80.0f, 220.0f, true }, { "op2_ratio", 1.0f, 2.0f }, { "op2_level", 0.1f, 0.4f } },
           11, 3, { "digital" } });
    add ({ "perc.blip", "perc", { "Blip" },
           { { "fm_algo", 3 }, { "fm_harm", -1.0f }, { "op2_sustain", 0.0f }, { "amp_release", 15.0f } },
           { { "fm_pitch", 12.0f, 30.0f }, { "op2_ratio", 1.0f, 4.4f }, { "op2_level", 0.2f, 0.6f }, { "op2_decay", 10.0f, 40.0f, true },
             { "amp_decay", 20.0f, 70.0f, true } },
           11, 3, { "digital" } });
    add ({ "perc.tamb", "perc", { "Tamb" },
           { { "fm_algo", 7 }, { "op1_wave", 1.0f }, { "op1_level", 1.0f }, { "op2_fixed", 1 }, { "op2_wave", 0.75f },
             { "op2_decay", 80.0f }, { "op2_sustain", 0.0f }, { "flt_type", 1 }, { "amp_release", 40.0f } },
           { { "op2_freq", 3000.0f, 5000.0f, true }, { "op2_level", 0.1f, 0.2f }, { "amp_attack", 2.0f, 5.0f },
             { "amp_decay", 120.0f, 260.0f, true }, { "flt_cutoff", 5000.0f, 8000.0f, true } },
           9, 2, { "organic" } });
    add ({ "perc.layer", "perc", { "Perc", "Hit" },
           { { "src_mode", 2 }, { "fm_algo", 3 }, { "op2_ratio", 1.5f }, { "op2_decay", 40.0f }, { "op2_sustain", 0.0f },
             { "amp_release", 50.0f } },
           { { "balance", 0.4f, 0.6f }, { "fm_pitch", -16.0f, 2.0f }, { "pitch_amt", 6.0f, 16.0f }, { "pitch_decay", 40.0f, 150.0f, true },
             { "amp_decay", 150.0f, 400.0f, true }, { "op2_level", 0.05f, 0.25f } },
           11, 3, { "digital" }, [] (uint32_t seed) { return makeTransient (seed); } });

    // Bass and stabs ------------------------------------------------------------------
    add ({ "bass.sub", "bass", { "Sub" },
           { { "play_mode", 1 }, { "fm_pitch", -24.0f }, { "fm_algo", 3 }, { "amp_decay", 400.0f }, { "op1_release", 300.0f },
             { "op2_ratio", 2.0f }, { "op2_sustain", 0.3f } },
           { { "glide", 0.0f, 60.0f }, { "amp_attack", 2.0f, 6.0f }, { "amp_sustain", 0.6f, 1.0f },
             { "amp_release", 60.0f, 300.0f, true }, { "drive", 0.0f, 0.35f }, { "flt_cutoff", 150.0f, 1500.0f, true },
             { "op2_level", 0.0f, 0.25f }, { "op2_decay", 60.0f, 400.0f, true } },
           13, 4, { "warm" } });
    add ({ "bass.808", "bass", { "808" },
           { { "fm_pitch", -24.0f }, { "amp_release", 200.0f }, { "op1_decay", 8000.0f } },
           { { "pitch_amt", 0.0f, 16.0f }, { "pitch_decay", 20.0f, 120.0f, true }, { "amp_decay", 700.0f, 3000.0f, true },
             { "glide", 30.0f, 120.0f, true }, { "drive", 0.0f, 0.3f }, { "punch", 0.1f, 0.5f } },
           13, 4, { "warm" } });
    add ({ "bass.808dist", "bass", { "808" },
           { { "fm_pitch", -24.0f }, { "amp_release", 200.0f }, { "op1_decay", 8000.0f } },
           { { "pitch_amt", 4.0f, 12.0f }, { "pitch_decay", 25.0f, 70.0f, true }, { "amp_decay", 900.0f, 2200.0f, true },
             { "glide", 40.0f, 120.0f, true }, { "drive", 0.5f, 0.85f }, { "drive_type", 0.5f, 2.49f },
             { "flt_cutoff", 2500.0f, 6000.0f, true }, { "punch", 0.2f, 0.4f } },
           11, 3, { "harsh" } });
    add ({ "bass.growl", "bass", { "Bass", "Growl" },
           { { "play_mode", 1 }, { "fm_pitch", -24.0f }, { "fm_algo", 3 }, { "fm_harm", -1.0f }, { "amp_sustain", 0.8f },
             { "amp_decay", 300.0f }, { "amp_release", 90.0f }, { "op2_sustain", 0.7f } },
           { { "op2_ratio", 0.5f, 2.49f }, { "op2_level", 0.4f, 0.8f }, { "op2_decay", 150.0f, 600.0f, true },
             { "fm_feedback", 0.1f, 0.4f }, { "flt_cutoff", 700.0f, 2000.0f, true }, { "flt_res", 0.2f, 0.5f },
             { "drive", 0.1f, 0.35f }, { "glide", 0.0f, 60.0f } },
           11, 3, { "harsh" } });
    add ({ "bass.pluck", "bass", { "Pluck" },
           { { "fm_algo", 3 }, { "fm_harm", -1.0f }, { "op2_sustain", 0.0f }, { "amp_release", 60.0f } },
           { { "fm_pitch", -24.0f, -12.0f }, { "op2_ratio", 1.0f, 3.4f }, { "op2_level", 0.4f, 0.7f },
             { "op2_decay", 60.0f, 200.0f, true }, { "amp_decay", 180.0f, 400.0f, true } },
           11, 3, { "warm" } });
    add ({ "bass.acid", "bass", { "Acid" },
           { { "play_mode", 1 }, { "fm_pitch", -12.0f }, { "fm_algo", 3 }, { "op1_wave", 0.5f }, { "op2_ratio", 1.0f },
             { "op2_sustain", 0.0f }, { "amp_sustain", 0.7f }, { "amp_decay", 300.0f }, { "amp_release", 50.0f }, { "drive_type", 1 } },
           { { "flt_cutoff", 300.0f, 2000.0f, true }, { "flt_res", 0.55f, 0.9f }, { "op2_level", 0.1f, 0.6f },
             { "op2_decay", 60.0f, 400.0f, true }, { "glide", 30.0f, 100.0f }, { "drive", 0.1f, 0.5f }, { "op1_wave", 0.45f, 0.7f } },
           11, 3, { "harsh" } });
    add ({ "bass.stab", "bass", { "Stab", "Organ" },
           { { "fm_algo", 7 }, { "op1_ratio", 1.0f }, { "op2_ratio", 2.0f }, { "op3_ratio", 3.0f }, { "op4_ratio", 4.0f },
             { "op1_level", 0.6f }, { "op2_level", 0.4f }, { "op3_level", 0.25f }, { "op4_level", 0.15f },
             { "op1_sustain", 0.2f }, { "op2_sustain", 0.1f }, { "op3_sustain", 0.0f }, { "op4_sustain", 0.0f },
             { "amp_release", 80.0f } },
           { { "fm_pitch", -12.0f, 0.0f }, { "op1_decay", 150.0f, 300.0f, true }, { "op2_decay", 120.0f, 300.0f, true },
             { "op3_decay", 80.0f, 250.0f, true }, { "op4_decay", 60.0f, 200.0f, true }, { "amp_decay", 180.0f, 350.0f, true },
             { "flt_cutoff", 2000.0f, 5000.0f, true } },
           9, 2, { "warm" } });

    // FX ---------------------------------------------------------------------------------
    add ({ "fx.riser", "fx", { "Riser" },
           { { "fm_algo", 7 }, { "op1_wave", 1.0f }, { "op1_level", 0.8f }, { "op4_wave", 1.0f }, { "op4_level", 0.8f },
             { "op2_level", 0.5f }, { "flt_type", 1 }, { "amp_release", 60.0f } },
           { { "fm_pitch", 12.0f, 24.0f }, { "pitch_amt", -36.0f, -18.0f }, { "pitch_decay", 800.0f, 2000.0f, true },
             { "amp_attack", 800.0f, 2000.0f, true }, { "amp_decay", 60.0f, 200.0f, true }, { "flt_cutoff", 500.0f, 1500.0f, true },
             { "flt_res", 0.1f, 0.4f } },
           11, 3, {} });
    add ({ "fx.sweep", "fx", { "Sweep" },
           { { "fm_algo", 3 }, { "op2_sustain", 0.3f }, { "amp_release", 100.0f } },
           { { "fm_pitch", -12.0f, 12.0f }, { "pitch_amt", 24.0f, 48.0f }, { "pitch_decay", 300.0f, 1200.0f, true },
             { "amp_decay", 500.0f, 1200.0f, true }, { "op2_ratio", 1.0f, 3.0f }, { "op2_level", 0.1f, 0.3f } },
           11, 3, { "digital" } });
    add ({ "fx.burst", "fx", { "Burst" },
           { { "fm_algo", 7 }, { "op1_wave", 1.0f }, { "op1_level", 1.0f }, { "drive_type", 3 }, { "flt_type", 2 },
             { "amp_release", 60.0f } },
           { { "drive", 0.6f, 1.0f }, { "flt_cutoff", 500.0f, 3000.0f, true }, { "flt_res", 0.5f, 0.8f },
             { "amp_decay", 100.0f, 300.0f, true } },
           9, 2, { "harsh", "digital" } });
    add ({ "fx.reverse", "fx", { "Reverse" }, join (kSamplePlays, { { "smp_reverse", 1 }, { "smp_fade_in", 20.0f } }),
           { { "smp_tune", -3.0f, 3.0f } },
           11, 3, {}, [] (uint32_t seed) { return makeReverse (seed); } });
    add ({ "fx.glitch", "fx", { "Glitch", "Stutter" }, kSamplePlays,
           { { "smp_tune", -2.0f, 2.0f } },
           11, 3, { "digital" }, [] (uint32_t seed) { return makeStutter (seed); } });
    add ({ "fx.impact", "fx", { "Impact" },
           { { "fm_algo", 7 }, { "op2_wave", 1.0f }, { "op2_sustain", 0.0f }, { "op1_sustain", 0.0f }, { "op1_decay", 3000.0f },
             { "flt_type", 0 }, { "amp_release", 300.0f } },
           { { "fm_pitch", -36.0f, -28.0f }, { "op2_level", 0.3f, 0.6f }, { "op2_decay", 400.0f, 900.0f, true },
             { "pitch_amt", 12.0f, 24.0f }, { "pitch_decay", 200.0f, 500.0f, true }, { "amp_decay", 1200.0f, 2500.0f, true },
             { "drive", 0.2f, 0.4f }, { "flt_cutoff", 3000.0f, 6000.0f, true } },
           9, 2, { "warm" } });

    // Textures ---------------------------------------------------------------------------
    add ({ "texture.loop", "texture", { "Loop" },
           join (kSamplePlays, { { "smp_slice_mode", 1 }, { "smp_slices", 16.0f } }),
           {}, 13, 4, {}, [] (uint32_t seed) { return makeLoop (seed); }, true });
    add ({ "texture.drone", "texture", { "Drone" },
           { { "play_mode", 1 }, { "fm_algo", 7 }, { "op1_ratio", 1.0f }, { "op3_ratio", 2.0f }, { "op4_ratio", 0.5f },
             { "op1_level", 0.5f }, { "op2_level", 0.5f }, { "op3_level", 0.25f }, { "op4_level", 0.3f },
             { "op1_release", 3000.0f }, { "op2_release", 3000.0f }, { "op3_release", 3000.0f }, { "op4_release", 3000.0f },
             { "amp_sustain", 1.0f }, { "flt_res", 0.3f } },
           { { "fm_pitch", -24.0f, -12.0f }, { "op2_ratio", 1.003f, 1.008f }, { "op1_wave", 0.0f, 0.3f }, { "op2_wave", 0.0f, 0.3f },
             { "amp_attack", 200.0f, 800.0f, true }, { "amp_release", 800.0f, 2000.0f, true }, { "flt_cutoff", 800.0f, 2500.0f, true } },
           11, 3, { "warm" } });
    add ({ "texture.crackle", "texture", { "Crackle", "Dust" }, kSamplePlays,
           {}, 11, 3, { "organic" }, [] (uint32_t seed) { return makeCrackle (seed); }, true });
    add ({ "texture.bed", "texture", { "Hiss", "Bed" },
           { { "play_mode", 1 }, { "fm_algo", 7 }, { "op1_wave", 1.0f }, { "op1_level", 1.0f }, { "amp_sustain", 0.8f },
             { "amp_decay", 400.0f } },
           { { "flt_cutoff", 1500.0f, 5000.0f, true }, { "amp_attack", 50.0f, 300.0f, true }, { "amp_release", 300.0f, 900.0f, true } },
           9, 2, { "organic" } });
    add ({ "texture.chord", "texture", { "Chord" },
           { { "fm_algo", 7 }, { "op1_ratio", 1.0f }, { "op2_ratio", 1.1892f }, { "op3_ratio", 1.4983f }, { "op4_ratio", 1.7818f },
             { "op1_level", 0.5f }, { "op2_level", 0.45f }, { "op3_level", 0.45f }, { "op4_level", 0.4f },
             { "op1_sustain", 0.0f }, { "op2_sustain", 0.0f }, { "op3_sustain", 0.0f }, { "op4_sustain", 0.0f },
             { "amp_release", 150.0f } },
           { { "fm_pitch", -12.0f, -5.0f }, { "flt_cutoff", 600.0f, 1500.0f, true }, { "flt_res", 0.3f, 0.5f },
             { "op1_decay", 250.0f, 700.0f, true }, { "op2_decay", 250.0f, 700.0f, true }, { "op3_decay", 250.0f, 700.0f, true },
             { "op4_decay", 250.0f, 700.0f, true }, { "amp_decay", 250.0f, 600.0f, true },
             { "op1_wave", 0.0f, 0.4f }, { "op2_wave", 0.0f, 0.4f }, { "op3_wave", 0.0f, 0.4f }, { "op4_wave", 0.0f, 0.4f } },
           13, 4, { "warm" } });

    // Aggressive (added after the first listen): hotter, driven, punchy. They
    // sit 3 dB above their category and use the Drive stage's Hard, Fold or
    // Crush, or a crushed transient layered over the body.
    auto hot = [&] (Archetype x) { x.hotDb = 3.0f; add (std::move (x)); };

    hot ({ "kick.hard", "kick", { "Kick", "Slam" },
           { { "fm_algo", 3 }, { "op2_ratio", 2.0f }, { "op2_sustain", 0.0f }, { "op2_release", 10.0f }, { "drive_type", 1 },
             { "amp_release", 40.0f } },
           { { "fm_pitch", -29.0f, -25.0f }, { "pitch_amt", 24.0f, 36.0f }, { "pitch_decay", 25.0f, 60.0f, true },
             { "amp_decay", 250.0f, 450.0f, true }, { "op2_level", 0.1f, 0.2f }, { "op2_decay", 8.0f, 15.0f },
             { "punch", 0.5f, 0.8f }, { "drive", 0.35f, 0.65f }, { "flt_cutoff", 4000.0f, 10000.0f, true } },
           14, 4, { "harsh" } });
    hot ({ "kick.gabber", "kick", { "Kick", "Hammer" },
           { { "fm_algo", 3 }, { "op2_ratio", 1.0f }, { "op2_level", 0.1f }, { "op2_decay", 40.0f }, { "op2_sustain", 0.0f },
             { "amp_release", 60.0f }, { "punch", 0.4f } },
           { { "fm_pitch", -26.0f, -21.0f }, { "pitch_amt", 18.0f, 30.0f }, { "pitch_decay", 40.0f, 100.0f, true },
             { "amp_decay", 400.0f, 900.0f, true }, { "drive", 0.6f, 0.9f }, { "drive_type", 0.0f, 1.49f },
             { "flt_cutoff", 2500.0f, 6000.0f, true }, { "flt_res", 0.1f, 0.3f } },
           11, 3, { "harsh" } });
    hot ({ "kick.fold", "kick", { "Kick" },
           { { "fm_algo", 3 }, { "op2_ratio", 1.5f }, { "op2_level", 0.1f }, { "op2_decay", 25.0f }, { "op2_sustain", 0.0f },
             { "drive_type", 2 }, { "amp_release", 40.0f }, { "punch", 0.4f } },
           { { "fm_pitch", -30.0f, -26.0f }, { "pitch_amt", 20.0f, 32.0f }, { "pitch_decay", 30.0f, 90.0f, true },
             { "amp_decay", 250.0f, 500.0f, true }, { "drive", 0.3f, 0.6f }, { "flt_cutoff", 3000.0f, 8000.0f, true } },
           11, 3, { "harsh", "digital" } });
    hot ({ "kick.punch", "kick", { "Punch", "Kick" },
           { { "src_mode", 2 }, { "fm_algo", 3 }, { "op2_ratio", 2.0f }, { "op2_level", 0.1f }, { "op2_decay", 15.0f },
             { "op2_sustain", 0.0f }, { "amp_release", 40.0f }, { "drive_type", 0 } },
           { { "balance", 0.55f, 0.7f }, { "fm_pitch", -29.0f, -25.0f }, { "pitch_amt", 20.0f, 32.0f },
             { "pitch_decay", 30.0f, 80.0f, true }, { "amp_decay", 250.0f, 500.0f, true }, { "punch", 0.5f, 0.8f },
             { "drive", 0.2f, 0.4f } },
           11, 3, { "harsh" }, [] (uint32_t seed) { return makeTransient (seed); } });

    hot ({ "snare.slam", "snare", { "Snare", "Slam" }, join (snareBody, { { "drive_type", 1 } }),
           { { "fm_pitch", -6.0f, 0.0f }, { "op1_level", 0.5f, 0.7f }, { "op1_decay", 80.0f, 160.0f, true },
             { "op2_ratio", 1.4f, 1.9f }, { "op2_level", 0.2f, 0.35f }, { "op2_decay", 50.0f, 100.0f, true },
             { "op3_level", 0.9f, 1.0f }, { "op3_decay", 160.0f, 300.0f, true }, { "pitch_amt", 6.0f, 12.0f },
             { "pitch_decay", 20.0f, 45.0f }, { "amp_decay", 180.0f, 320.0f, true }, { "punch", 0.5f, 0.8f },
             { "drive", 0.35f, 0.7f } },
           14, 4, { "harsh" } });
    hot ({ "snare.fold", "snare", { "Snare" }, join (snareBody, { { "drive_type", 2 } }),
           { { "fm_pitch", -8.0f, 2.0f }, { "op1_level", 0.5f, 0.7f }, { "op1_decay", 80.0f, 160.0f, true },
             { "op2_ratio", 1.4f, 2.2f }, { "op2_level", 0.2f, 0.4f }, { "op2_decay", 50.0f, 100.0f, true },
             { "op3_level", 0.8f, 1.0f }, { "op3_decay", 150.0f, 300.0f, true }, { "amp_decay", 150.0f, 300.0f, true },
             { "punch", 0.4f, 0.7f }, { "drive", 0.2f, 0.5f } },
           9, 2, { "harsh", "digital" } });
    hot ({ "snare.clapdist", "snare", { "Clap" }, join (kSamplePlays, { { "drive_type", 1 }, { "flt_type", 1 } }),
           { { "drive", 0.3f, 0.6f }, { "flt_cutoff", 250.0f, 600.0f, true }, { "punch", 0.3f, 0.6f } },
           11, 3, { "harsh" }, [] (uint32_t seed) { return makeClap (seed + 101); } });
    hot ({ "snare.rimhard", "snare", { "Rim" },
           { { "fm_algo", 7 }, { "op1_sustain", 0.0f }, { "op1_release", 20.0f }, { "op2_sustain", 0.0f }, { "op2_release", 20.0f },
             { "op3_wave", 1.0f }, { "op3_sustain", 0.0f }, { "op3_release", 10.0f }, { "amp_release", 30.0f }, { "flt_type", 1 },
             { "drive_type", 1 } },
           { { "fm_pitch", 6.0f, 14.0f }, { "op1_level", 0.8f, 1.0f }, { "op1_decay", 30.0f, 70.0f },
             { "op2_ratio", 2.9f, 3.9f }, { "op2_level", 0.6f, 0.9f }, { "op2_decay", 25.0f, 50.0f },
             { "op3_level", 0.5f, 0.8f }, { "op3_decay", 10.0f, 20.0f }, { "amp_decay", 60.0f, 110.0f },
             { "flt_cutoff", 250.0f, 500.0f, true }, { "drive", 0.4f, 0.7f }, { "punch", 0.3f, 0.6f } },
           9, 2, { "harsh" } });

    hot ({ "hat.hard", "hat", { "Hat" }, join (hatBase, { { "drive_type", 1 }, { "amp_release", 20.0f } }),
           { { "op2_freq", 480.0f, 620.0f }, { "op3_freq", 740.0f, 900.0f }, { "op2_level", 0.05f, 0.2f }, { "op3_level", 0.05f, 0.2f },
             { "amp_decay", 30.0f, 90.0f, true }, { "flt_cutoff", 7000.0f, 11000.0f, true }, { "flt_res", 0.1f, 0.3f },
             { "drive", 0.4f, 0.8f } },
           11, 3, { "harsh" } });
    hot ({ "hat.openhard", "hat", { "Open Hat" }, join (hatBase, { { "drive_type", 2 }, { "amp_release", 60.0f } }),
           { { "op2_freq", 480.0f, 620.0f }, { "op3_freq", 740.0f, 900.0f }, { "op2_level", 0.05f, 0.2f }, { "op3_level", 0.05f, 0.2f },
             { "amp_decay", 250.0f, 600.0f, true }, { "flt_cutoff", 6000.0f, 10000.0f, true }, { "flt_res", 0.1f, 0.3f },
             { "drive", 0.2f, 0.5f } },
           9, 2, { "harsh" } });
    hot ({ "hat.metalhard", "hat", { "Hat" },
           { { "fm_algo", 4 }, { "op1_level", 0.6f }, { "op2_level", 0.5f }, { "op2_ratio", 1.41f }, { "op3_level", 0.4f },
             { "op3_ratio", 2.23f }, { "flt_type", 1 }, { "amp_release", 30.0f }, { "drive_type", 1 } },
           { { "fm_pitch", 24.0f, 36.0f }, { "op4_ratio", 1.4f, 3.3f }, { "op4_level", 0.5f, 0.9f }, { "fm_harm", 0.5f, 1.0f },
             { "fm_feedback", 0.3f, 0.6f }, { "amp_decay", 30.0f, 120.0f, true }, { "flt_cutoff", 5000.0f, 8000.0f, true },
             { "drive", 0.4f, 0.7f } },
           9, 2, { "harsh", "metallic" } });

    hot ({ "perc.tomhard", "perc", { "Tom" },
           { { "fm_algo", 3 }, { "op2_ratio", 1.5f }, { "op2_decay", 60.0f }, { "op2_sustain", 0.0f }, { "op2_release", 40.0f },
             { "amp_release", 60.0f } },
           { { "fm_pitch", -20.0f, -4.0f }, { "pitch_amt", 8.0f, 18.0f }, { "pitch_decay", 80.0f, 250.0f, true },
             { "amp_decay", 250.0f, 600.0f, true }, { "op2_level", 0.1f, 0.3f }, { "punch", 0.4f, 0.7f },
             { "drive", 0.4f, 0.8f }, { "drive_type", 0.5f, 2.49f } },
           11, 3, { "harsh" } });
    hot ({ "perc.clang", "perc", { "Clang" },
           { { "fm_algo", 4 }, { "op1_ratio", 1.0f }, { "op1_level", 0.6f }, { "op2_level", 0.5f }, { "op3_level", 0.5f },
             { "op1_sustain", 0.0f }, { "op2_sustain", 0.0f }, { "op3_sustain", 0.0f }, { "op4_sustain", 0.0f },
             { "drive_type", 1 }, { "amp_release", 60.0f } },
           { { "fm_pitch", 0.0f, 12.0f }, { "fm_harm", 0.6f, 1.0f }, { "op2_ratio", 1.3f, 1.7f }, { "op3_ratio", 2.0f, 2.9f },
             { "op4_ratio", 2.5f, 4.5f }, { "op4_level", 0.4f, 0.8f }, { "op1_decay", 100.0f, 300.0f, true },
             { "op2_decay", 100.0f, 300.0f, true }, { "op3_decay", 80.0f, 250.0f, true }, { "op4_decay", 50.0f, 150.0f, true },
             { "amp_decay", 100.0f, 300.0f, true }, { "drive", 0.3f, 0.6f } },
           11, 3, { "harsh", "metallic" } });
    hot ({ "perc.zaphard", "perc", { "Zap" },
           { { "fm_algo", 3 }, { "op2_sustain", 0.0f }, { "op2_decay", 60.0f }, { "amp_release", 30.0f }, { "drive_type", 3 } },
           { { "fm_pitch", -2.0f, 14.0f }, { "pitch_amt", 30.0f, 48.0f }, { "pitch_decay", 20.0f, 80.0f, true },
             { "amp_decay", 80.0f, 220.0f, true }, { "op2_ratio", 1.0f, 2.0f }, { "op2_level", 0.2f, 0.5f },
             { "drive", 0.4f, 0.8f } },
           9, 2, { "harsh", "digital" } });

    hot ({ "bass.reese", "bass", { "Reese" },
           { { "play_mode", 1 }, { "fm_pitch", -24.0f }, { "fm_algo", 7 }, { "op1_wave", 0.5f }, { "op1_ratio", 1.0f },
             { "op2_wave", 0.5f }, { "op2_level", 0.9f }, { "op1_level", 0.9f }, { "amp_sustain", 0.9f }, { "amp_decay", 300.0f },
             { "amp_release", 80.0f }, { "drive_type", 0 } },
           { { "op2_ratio", 1.006f, 1.015f }, { "drive", 0.3f, 0.6f }, { "flt_cutoff", 600.0f, 2000.0f, true },
             { "flt_res", 0.2f, 0.4f }, { "glide", 0.0f, 60.0f } },
           11, 3, { "harsh" } });
    hot ({ "bass.808clip", "bass", { "808" },
           { { "fm_pitch", -24.0f }, { "amp_release", 200.0f }, { "op1_decay", 8000.0f }, { "drive_type", 1 } },
           { { "pitch_amt", 4.0f, 14.0f }, { "pitch_decay", 25.0f, 80.0f, true }, { "amp_decay", 600.0f, 1600.0f, true },
             { "glide", 40.0f, 120.0f, true }, { "drive", 0.6f, 0.95f }, { "punch", 0.4f, 0.6f },
             { "flt_cutoff", 3000.0f, 8000.0f, true } },
           11, 3, { "harsh" } });
    hot ({ "bass.growlhard", "bass", { "Growl", "Bass" },
           { { "play_mode", 1 }, { "fm_pitch", -24.0f }, { "fm_algo", 3 }, { "fm_harm", -1.0f }, { "amp_sustain", 0.8f },
             { "amp_decay", 300.0f }, { "amp_release", 90.0f }, { "op2_sustain", 0.7f }, { "drive_type", 2 } },
           { { "op2_ratio", 0.5f, 2.49f }, { "op2_level", 0.5f, 0.9f }, { "op2_decay", 150.0f, 600.0f, true },
             { "fm_feedback", 0.4f, 0.7f }, { "flt_cutoff", 800.0f, 2500.0f, true }, { "flt_res", 0.2f, 0.5f },
             { "drive", 0.3f, 0.6f } },
           9, 2, { "harsh" } });

    hot ({ "fx.blast", "fx", { "Blast", "Hit" },
           { { "fm_algo", 7 }, { "op2_wave", 1.0f }, { "op1_sustain", 0.0f }, { "op2_sustain", 0.0f }, { "op1_decay", 1500.0f },
             { "drive_type", 1 }, { "amp_release", 200.0f } },
           { { "fm_pitch", -34.0f, -26.0f }, { "op2_level", 0.4f, 0.8f }, { "op2_decay", 200.0f, 600.0f, true },
             { "pitch_amt", 18.0f, 36.0f }, { "pitch_decay", 60.0f, 250.0f, true }, { "amp_decay", 500.0f, 1500.0f, true },
             { "drive", 0.5f, 0.85f }, { "flt_cutoff", 3000.0f, 9000.0f, true } },
           9, 2, { "harsh" } });
    hot ({ "fx.screech", "fx", { "Screech" },
           { { "fm_algo", 0 }, { "op2_level", 0.5f }, { "op3_level", 0.4f }, { "op4_level", 0.4f }, { "drive_type", 1 },
             { "amp_release", 100.0f } },
           { { "fm_pitch", 12.0f, 24.0f }, { "pitch_amt", -12.0f, 12.0f }, { "pitch_decay", 200.0f, 800.0f, true },
             { "fm_feedback", 0.5f, 0.9f }, { "op2_ratio", 1.0f, 3.0f }, { "op3_ratio", 0.5f, 2.0f },
             { "amp_decay", 300.0f, 800.0f, true }, { "drive", 0.4f, 0.7f } },
           9, 2, { "harsh", "digital" } });

    // Moods (after the second listen: the sets leaned too much on noise).
    // Tonal recipes, one group per mood tag: neutral, synth-aggressive, neon,
    // tender. Hats come from square-wave clusters or FM metal, snares from
    // tuned bodies with at most a trace of noise; a result that measures
    // noisier than `maxFlatness` is a dud.
    auto mood = [&] (Archetype x, float maxFlat, float hot = 0.0f)
    {
        x.maxFlatness = maxFlat;
        x.hotDb = hot;
        add (std::move (x));
    };
    const Overrides squareHat { { "fm_algo", 7 }, { "op1_fixed", 1 }, { "op2_fixed", 1 }, { "op3_fixed", 1 }, { "op4_fixed", 1 },
                                { "op1_wave", 0.75f }, { "op2_wave", 0.75f }, { "op3_wave", 0.75f }, { "op4_wave", 0.75f },
                                { "op1_level", 0.6f }, { "op2_level", 0.6f }, { "op3_level", 0.6f }, { "op4_level", 0.6f },
                                { "flt_type", 1 }, { "amp_release", 20.0f } };

    // Neutral: clean and balanced.
    mood ({ "neutral.kick", "kick", { "Kick" },
            { { "fm_algo", 3 }, { "op2_ratio", 1.0f }, { "op2_decay", 20.0f }, { "op2_sustain", 0.0f }, { "amp_release", 50.0f } },
            { { "fm_pitch", -30.0f, -27.0f }, { "pitch_amt", 14.0f, 22.0f }, { "pitch_decay", 40.0f, 90.0f, true },
              { "amp_decay", 320.0f, 520.0f, true }, { "op2_level", 0.05f, 0.12f }, { "punch", 0.15f, 0.3f } },
            10, 3, { "neutral" } }, 0.3f);
    mood ({ "neutral.snare", "snare", { "Snare" },
            { { "fm_algo", 5 }, { "op1_level", 0.6f }, { "op1_sustain", 0.0f }, { "op2_level", 0.35f }, { "op2_sustain", 0.0f },
              { "op3_sustain", 0.0f }, { "op4_sustain", 0.0f }, { "amp_release", 40.0f }, { "punch", 0.25f } },
            { { "fm_pitch", -5.0f, 0.0f }, { "op1_decay", 90.0f, 150.0f, true }, { "op2_ratio", 1.47f, 1.62f },
              { "op2_decay", 60.0f, 110.0f, true }, { "op3_ratio", 4.1f, 5.3f }, { "op3_level", 0.3f, 0.45f },
              { "op3_decay", 60.0f, 140.0f, true }, { "op4_ratio", 6.9f, 9.3f }, { "op4_level", 0.4f, 0.6f },
              { "op4_decay", 40.0f, 90.0f, true }, { "fm_feedback", 0.15f, 0.3f }, { "pitch_amt", 4.0f, 8.0f },
              { "pitch_decay", 25.0f, 40.0f }, { "amp_decay", 180.0f, 280.0f, true }, { "flt_cutoff", 9000.0f, 14000.0f, true } },
            11, 3, { "neutral" } }, 0.3f);
    mood ({ "neutral.rim", "snare", { "Rim" },
            { { "fm_algo", 3 }, { "op2_sustain", 0.0f }, { "flt_type", 1 }, { "amp_release", 15.0f } },
            { { "fm_pitch", 10.0f, 16.0f }, { "op2_ratio", 3.2f, 3.6f }, { "op2_level", 0.3f, 0.5f }, { "op2_decay", 10.0f, 20.0f },
              { "amp_decay", 30.0f, 60.0f }, { "flt_cutoff", 300.0f, 500.0f, true } },
            9, 2, { "neutral" } }, 0.3f);
    mood ({ "neutral.hat", "hat", { "Hat" }, squareHat,
            { { "op1_freq", 300.0f, 320.0f }, { "op2_freq", 540.0f, 560.0f }, { "op3_freq", 790.0f, 810.0f },
              { "op4_freq", 1045.0f, 1080.0f }, { "flt_cutoff", 6500.0f, 9000.0f, true }, { "flt_res", 0.1f, 0.2f },
              { "amp_decay", 35.0f, 80.0f, true } },
            11, 3, { "neutral" } }, 0.45f);
    mood ({ "neutral.openhat", "hat", { "Open Hat" }, squareHat,
            { { "op1_freq", 300.0f, 320.0f }, { "op2_freq", 540.0f, 560.0f }, { "op3_freq", 790.0f, 810.0f },
              { "op4_freq", 1045.0f, 1080.0f }, { "flt_cutoff", 6500.0f, 9000.0f, true }, { "flt_res", 0.1f, 0.2f },
              { "amp_decay", 250.0f, 500.0f, true } },
            9, 2, { "neutral" } }, 0.45f);
    mood ({ "neutral.tom", "perc", { "Tom" },
            { { "fm_algo", 3 }, { "op2_ratio", 2.0f }, { "op2_decay", 30.0f }, { "op2_sustain", 0.0f }, { "amp_release", 60.0f } },
            { { "fm_pitch", -18.0f, -6.0f }, { "pitch_amt", 5.0f, 10.0f }, { "pitch_decay", 100.0f, 200.0f, true },
              { "amp_decay", 250.0f, 500.0f, true }, { "op2_level", 0.05f, 0.1f } },
            9, 2, { "neutral" } }, 0.3f);
    mood ({ "neutral.bass", "bass", { "Bass" },
            { { "play_mode", 1 }, { "fm_pitch", -24.0f }, { "fm_algo", 3 }, { "fm_harm", -1.0f }, { "op2_sustain", 0.2f },
              { "amp_decay", 300.0f } },
            { { "op2_ratio", 0.9f, 2.4f }, { "op2_level", 0.1f, 0.4f }, { "op2_decay", 100.0f, 500.0f, true },
              { "amp_sustain", 0.6f, 0.95f }, { "amp_release", 60.0f, 200.0f, true },
              { "flt_cutoff", 500.0f, 2500.0f, true }, { "glide", 0.0f, 40.0f } },
            9, 2, { "neutral" } }, 0.3f);

    // Synth-aggressive: saw, square and FM tones, driven hard; no noise.
    mood ({ "synth.kick", "kick", { "Kick" },
            { { "fm_algo", 3 }, { "op2_ratio", 1.0f }, { "op2_sustain", 0.0f }, { "drive_type", 1 }, { "punch", 0.4f },
              { "amp_release", 40.0f } },
            { { "fm_pitch", -29.0f, -25.0f }, { "pitch_amt", 24.0f, 38.0f }, { "pitch_decay", 25.0f, 60.0f, true },
              { "amp_decay", 250.0f, 450.0f, true }, { "op2_level", 0.35f, 0.6f }, { "op2_decay", 30.0f, 80.0f, true },
              { "op1_wave", 0.0f, 0.15f }, { "drive", 0.3f, 0.55f }, { "flt_cutoff", 5000.0f, 9000.0f, true } },
            11, 3, { "synth-aggressive" } }, 0.3f, 2.0f);
    mood ({ "synth.snare", "snare", { "Snare" },
            { { "fm_algo", 5 }, { "op1_level", 0.5f }, { "op1_sustain", 0.0f }, { "op2_ratio", 1.5f }, { "op2_level", 0.3f },
              { "op2_sustain", 0.0f }, { "op3_level", 0.4f }, { "op3_sustain", 0.0f }, { "op4_sustain", 0.0f },
              { "drive_type", 1 }, { "amp_release", 40.0f } },
            { { "op1_wave", 0.5f, 0.75f }, { "op1_decay", 80.0f, 150.0f, true }, { "op2_decay", 60.0f, 100.0f, true },
              { "op3_ratio", 3.3f, 4.7f }, { "op3_decay", 80.0f, 160.0f, true }, { "op4_ratio", 5.5f, 8.5f },
              { "op4_level", 0.5f, 0.8f }, { "op4_decay", 50.0f, 120.0f, true }, { "fm_feedback", 0.2f, 0.45f },
              { "fm_pitch", -3.0f, 4.0f }, { "pitch_amt", 8.0f, 16.0f }, { "pitch_decay", 20.0f, 40.0f },
              { "amp_decay", 150.0f, 260.0f, true }, { "drive", 0.35f, 0.6f }, { "punch", 0.4f, 0.6f } },
            11, 3, { "synth-aggressive" } }, 0.35f, 2.0f);
    mood ({ "synth.zap", "perc", { "Zap", "Laser" },
            { { "op1_wave", 0.75f }, { "drive_type", 1 }, { "amp_release", 30.0f } },
            { { "fm_pitch", 6.0f, 18.0f }, { "pitch_amt", 24.0f, 40.0f }, { "pitch_decay", 30.0f, 90.0f, true },
              { "amp_decay", 90.0f, 220.0f, true }, { "drive", 0.3f, 0.5f }, { "flt_cutoff", 3000.0f, 7000.0f, true },
              { "flt_res", 0.3f, 0.5f } },
            9, 2, { "synth-aggressive" } }, 0.3f, 2.0f);
    const Overrides fmHat { { "fm_algo", 4 }, { "op1_wave", 0.75f }, { "op2_wave", 0.75f }, { "op3_wave", 0.75f },
                            { "op1_level", 0.5f }, { "op2_level", 0.5f }, { "op2_ratio", 1.48f }, { "op3_level", 0.5f },
                            { "op3_ratio", 2.13f }, { "flt_type", 1 }, { "drive_type", 1 }, { "amp_release", 30.0f } };
    mood ({ "synth.hat", "hat", { "Hat" }, fmHat,
            { { "op4_ratio", 3.1f, 5.7f }, { "op4_level", 0.5f, 0.8f }, { "fm_pitch", 24.0f, 32.0f }, { "fm_feedback", 0.1f, 0.3f },
              { "flt_cutoff", 6000.0f, 9000.0f, true }, { "amp_decay", 40.0f, 100.0f, true }, { "drive", 0.3f, 0.5f } },
            9, 2, { "synth-aggressive" } }, 0.45f, 2.0f);
    mood ({ "synth.openhat", "hat", { "Open Hat" }, fmHat,
            { { "op4_ratio", 3.1f, 5.7f }, { "op4_level", 0.5f, 0.8f }, { "fm_pitch", 24.0f, 32.0f }, { "fm_feedback", 0.1f, 0.3f },
              { "flt_cutoff", 6000.0f, 9000.0f, true }, { "amp_decay", 250.0f, 450.0f, true }, { "drive", 0.3f, 0.5f } },
            9, 2, { "synth-aggressive" } }, 0.45f, 2.0f);
    mood ({ "synth.stab", "bass", { "Stab" },
            { { "fm_algo", 7 }, { "fm_pitch", -12.0f }, { "op1_wave", 0.5f }, { "op2_wave", 0.5f }, { "op3_wave", 0.5f },
              { "op4_wave", 0.5f }, { "op1_level", 0.5f }, { "op2_level", 0.5f }, { "op3_ratio", 1.4983f }, { "op3_level", 0.35f },
              { "op4_ratio", 2.0f }, { "op4_level", 0.3f }, { "op1_sustain", 0.1f }, { "op2_sustain", 0.1f },
              { "op3_sustain", 0.1f }, { "op4_sustain", 0.1f }, { "drive_type", 1 }, { "amp_release", 80.0f } },
            { { "op2_ratio", 1.006f, 1.012f }, { "op1_decay", 150.0f, 350.0f, true }, { "op2_decay", 150.0f, 350.0f, true },
              { "op3_decay", 150.0f, 350.0f, true }, { "op4_decay", 150.0f, 350.0f, true }, { "amp_decay", 180.0f, 350.0f, true },
              { "flt_cutoff", 1500.0f, 4000.0f, true }, { "flt_res", 0.3f, 0.5f }, { "drive", 0.3f, 0.55f } },
            9, 2, { "synth-aggressive" } }, 0.3f, 2.0f);
    mood ({ "synth.tom", "perc", { "Tom" },
            { { "drive_type", 1 }, { "amp_release", 50.0f } },
            { { "op1_wave", 0.6f, 0.75f }, { "fm_pitch", -14.0f, -2.0f }, { "pitch_amt", 10.0f, 20.0f },
              { "pitch_decay", 60.0f, 160.0f, true }, { "amp_decay", 200.0f, 400.0f, true }, { "drive", 0.3f, 0.5f },
              { "flt_cutoff", 2000.0f, 5000.0f, true } },
            9, 2, { "synth-aggressive" } }, 0.3f, 2.0f);
    mood ({ "synth.bass", "bass", { "Bass" },
            { { "play_mode", 1 }, { "fm_pitch", -24.0f }, { "fm_algo", 3 }, { "fm_harm", -1.0f }, { "op1_wave", 0.5f },
              { "op2_sustain", 0.3f }, { "amp_sustain", 0.85f }, { "amp_decay", 300.0f }, { "amp_release", 80.0f },
              { "drive_type", 2 } },
            { { "op2_ratio", 0.9f, 2.4f }, { "op2_level", 0.3f, 0.6f }, { "op2_decay", 100.0f, 300.0f, true },
              { "drive", 0.2f, 0.4f }, { "flt_cutoff", 600.0f, 1800.0f, true }, { "flt_res", 0.3f, 0.6f },
              { "glide", 20.0f, 70.0f } },
            11, 3, { "synth-aggressive" } }, 0.3f, 2.0f);

    // Neon: bright, glossy synth percussion, bells and chords.
    mood ({ "neon.kick", "kick", { "Kick" },
            { { "fm_algo", 3 }, { "op2_sustain", 0.0f }, { "amp_release", 40.0f }, { "punch", 0.3f } },
            { { "fm_pitch", -28.0f, -25.0f }, { "pitch_amt", 20.0f, 30.0f }, { "pitch_decay", 30.0f, 60.0f, true },
              { "amp_decay", 250.0f, 400.0f, true }, { "op2_ratio", 6.0f, 8.0f }, { "op2_level", 0.1f, 0.2f },
              { "op2_decay", 5.0f, 10.0f } },
            9, 2, { "neon" } }, 0.3f);
    mood ({ "neon.snare", "snare", { "Snare" },
            { { "fm_algo", 7 }, { "op1_wave", 0.25f }, { "op1_level", 0.6f }, { "op1_sustain", 0.0f }, { "op2_level", 0.3f },
              { "op2_sustain", 0.0f }, { "op3_wave", 1.0f }, { "op3_sustain", 0.0f }, { "amp_release", 50.0f }, { "punch", 0.3f } },
            { { "fm_pitch", -2.0f, 4.0f }, { "op1_decay", 120.0f, 200.0f, true }, { "op2_ratio", 2.3f, 2.8f },
              { "op2_decay", 60.0f, 120.0f, true }, { "op3_level", 0.15f, 0.3f }, { "op3_decay", 150.0f, 250.0f, true },
              { "pitch_amt", 8.0f, 12.0f }, { "pitch_decay", 20.0f, 35.0f }, { "amp_decay", 220.0f, 350.0f, true } },
            11, 3, { "neon" } }, 0.35f);
    mood ({ "neon.bell", "perc", { "Bell" },
            { { "fm_algo", 3 }, { "op1_ratio", 1.0f }, { "op2_ratio", 3.5f }, { "op3_ratio", 1.0f }, { "op3_level", 0.5f },
              { "op4_level", 0.3f }, { "op1_sustain", 0.0f }, { "op2_sustain", 0.0f }, { "op3_sustain", 0.0f },
              { "op4_sustain", 0.0f }, { "amp_release", 300.0f } },
            { { "fm_pitch", 12.0f, 24.0f }, { "op2_level", 0.4f, 0.6f }, { "op2_decay", 400.0f, 900.0f, true },
              { "op4_ratio", 1.41f, 7.0f }, { "op4_decay", 300.0f, 800.0f, true }, { "op1_decay", 600.0f, 1400.0f, true },
              { "op3_decay", 600.0f, 1400.0f, true }, { "amp_decay", 600.0f, 1400.0f, true } },
            9, 2, { "neon" } }, 0.3f);
    mood ({ "neon.chord", "texture", { "Chord" },
            { { "fm_algo", 7 }, { "op1_ratio", 1.0f }, { "op2_ratio", 1.1892f }, { "op3_ratio", 1.4983f }, { "op4_ratio", 2.2449f },
              { "op1_level", 0.45f }, { "op2_level", 0.45f }, { "op3_level", 0.45f }, { "op4_level", 0.45f },
              { "op1_sustain", 0.2f }, { "op2_sustain", 0.2f }, { "op3_sustain", 0.2f }, { "op4_sustain", 0.2f },
              { "amp_release", 200.0f } },
            { { "fm_pitch", -12.0f, -5.0f }, { "op1_wave", 0.4f, 0.55f }, { "op2_wave", 0.4f, 0.55f }, { "op3_wave", 0.4f, 0.55f },
              { "op4_wave", 0.4f, 0.55f }, { "op1_decay", 300.0f, 700.0f, true }, { "op2_decay", 300.0f, 700.0f, true },
              { "op3_decay", 300.0f, 700.0f, true }, { "op4_decay", 300.0f, 700.0f, true }, { "amp_decay", 300.0f, 700.0f, true },
              { "flt_cutoff", 2500.0f, 6000.0f, true }, { "flt_res", 0.2f, 0.35f } },
            9, 2, { "neon" } }, 0.3f);
    mood ({ "neon.tom", "perc", { "Tom" },
            { { "amp_release", 60.0f }, { "flt_res", 0.2f } },
            { { "op1_wave", 0.25f, 0.4f }, { "fm_pitch", -12.0f, 0.0f }, { "pitch_amt", 12.0f, 24.0f },
              { "pitch_decay", 80.0f, 200.0f, true }, { "amp_decay", 250.0f, 500.0f, true }, { "flt_cutoff", 3000.0f, 8000.0f, true } },
            9, 2, { "neon" } }, 0.3f);
    mood ({ "neon.bass", "bass", { "Bass" },
            { { "play_mode", 1 }, { "fm_pitch", -24.0f }, { "fm_algo", 7 }, { "op1_wave", 0.5f }, { "op1_level", 0.6f },
              { "op2_wave", 0.5f }, { "op2_ratio", 2.0f }, { "op2_level", 0.25f }, { "amp_sustain", 0.8f }, { "amp_decay", 300.0f },
              { "amp_release", 80.0f } },
            { { "flt_cutoff", 900.0f, 2500.0f, true }, { "flt_res", 0.25f, 0.45f }, { "glide", 0.0f, 40.0f } },
            9, 2, { "neon" } }, 0.3f);
    mood ({ "neon.hat", "hat", { "Hat" },
            { { "fm_algo", 3 }, { "op1_ratio", 1.0f }, { "op2_sustain", 0.0f }, { "op3_ratio", 1.41f }, { "op3_level", 0.5f },
              { "op4_ratio", 13.0f }, { "op4_level", 0.5f }, { "op4_sustain", 0.0f }, { "op4_decay", 40.0f }, { "flt_type", 1 },
              { "amp_release", 20.0f } },
            { { "op2_ratio", 7.1f, 11.3f }, { "op2_level", 0.4f, 0.7f }, { "op2_decay", 20.0f, 60.0f }, { "fm_pitch", 30.0f, 38.0f },
              { "flt_cutoff", 5000.0f, 8000.0f, true }, { "amp_decay", 30.0f, 90.0f, true } },
            9, 2, { "neon" } }, 0.45f);
    mood ({ "neon.pluck", "bass", { "Pluck", "Arp" },
            { { "fm_algo", 3 }, { "op2_ratio", 1.0f }, { "op2_sustain", 0.0f }, { "amp_release", 60.0f } },
            { { "op1_wave", 0.7f, 0.75f }, { "op2_level", 0.2f, 0.4f }, { "op2_decay", 60.0f, 150.0f, true },
              { "fm_pitch", -12.0f, 0.0f }, { "amp_decay", 150.0f, 300.0f, true }, { "flt_cutoff", 1500.0f, 4000.0f, true },
              { "flt_res", 0.4f, 0.6f } },
            9, 2, { "neon" } }, 0.3f);

    // Tender: soft, round and gentle.
    mood ({ "tender.kick", "kick", { "Kick" },
            { { "fm_algo", 3 }, { "amp_release", 60.0f } },
            { { "fm_pitch", -30.0f, -27.0f }, { "pitch_amt", 6.0f, 12.0f }, { "pitch_decay", 50.0f, 120.0f, true },
              { "amp_attack", 1.0f, 3.0f }, { "amp_decay", 300.0f, 550.0f, true }, { "flt_cutoff", 800.0f, 2000.0f, true } },
            10, 3, { "tender", "warm" } }, 0.3f);
    mood ({ "tender.snare", "snare", { "Snare" },
            { { "fm_algo", 7 }, { "op1_level", 0.5f }, { "op1_sustain", 0.0f }, { "op2_wave", 0.25f }, { "op2_level", 0.35f },
              { "op2_sustain", 0.0f }, { "op3_wave", 1.0f }, { "op3_sustain", 0.0f }, { "op4_level", 0.25f }, { "op4_sustain", 0.0f },
              { "amp_release", 60.0f } },
            { { "fm_pitch", 2.0f, 8.0f }, { "op1_decay", 100.0f, 180.0f, true }, { "op2_ratio", 1.5f, 1.8f },
              { "op2_decay", 80.0f, 140.0f, true }, { "op3_level", 0.1f, 0.2f }, { "op3_decay", 120.0f, 220.0f, true },
              { "op4_ratio", 2.2f, 2.8f }, { "op4_decay", 40.0f, 90.0f, true },
              { "amp_attack", 2.0f, 6.0f }, { "amp_decay", 180.0f, 300.0f, true }, { "flt_cutoff", 5000.0f, 8000.0f, true } },
            9, 2, { "tender" } }, 0.3f);
    mood ({ "tender.rim", "snare", { "Knock", "Rim" },
            { { "fm_algo", 3 }, { "op2_ratio", 2.7f }, { "op2_decay", 10.0f }, { "op2_sustain", 0.0f }, { "amp_attack", 1.0f },
              { "amp_release", 20.0f } },
            { { "fm_pitch", 5.0f, 12.0f }, { "op2_level", 0.1f, 0.2f }, { "amp_decay", 30.0f, 70.0f },
              { "flt_cutoff", 3000.0f, 5000.0f, true } },
            9, 2, { "tender" } }, 0.3f);
    mood ({ "tender.hat", "hat", { "Hat" },
            { { "fm_algo", 4 }, { "op1_level", 0.4f }, { "op2_level", 0.4f }, { "op2_ratio", 1.48f }, { "op3_level", 0.4f },
              { "op3_ratio", 2.13f }, { "op4_ratio", 3.3f }, { "flt_type", 1 }, { "flt_cutoff", 3000.0f }, { "amp_release", 20.0f } },
            { { "fm_pitch", 36.0f, 42.0f }, { "op4_level", 0.2f, 0.35f }, { "amp_attack", 0.5f, 1.0f },
              { "amp_decay", 25.0f, 60.0f, true } },
            9, 2, { "tender" } }, 0.45f);
    mood ({ "tender.marimba", "perc", { "Mallet", "Marimba" },
            { { "fm_algo", 3 }, { "op2_ratio", 4.0f }, { "op2_sustain", 0.0f }, { "op3_ratio", 4.0f }, { "op3_sustain", 0.0f },
              { "op3_decay", 50.0f }, { "amp_release", 100.0f } },
            { { "fm_pitch", 0.0f, 12.0f }, { "op2_level", 0.25f, 0.45f }, { "op2_decay", 30.0f, 80.0f, true },
              { "op3_level", 0.05f, 0.12f }, { "amp_decay", 300.0f, 700.0f, true } },
            9, 2, { "tender" } }, 0.3f);
    mood ({ "tender.kalimba", "perc", { "Kalimba", "Tine" },
            { { "fm_algo", 3 }, { "op2_sustain", 0.0f }, { "amp_release", 200.0f } },
            { { "fm_pitch", 12.0f, 24.0f }, { "op2_ratio", 5.4f, 6.3f }, { "op2_level", 0.2f, 0.35f },
              { "op2_decay", 40.0f, 100.0f, true }, { "amp_decay", 400.0f, 900.0f, true } },
            9, 2, { "tender" } }, 0.3f);
    mood ({ "tender.bass", "bass", { "Bass" },
            { { "play_mode", 1 }, { "fm_pitch", -24.0f }, { "fm_algo", 3 }, { "op2_ratio", 2.0f }, { "amp_decay", 400.0f } },
            { { "op1_wave", 0.0f, 0.25f }, { "op2_level", 0.0f, 0.15f }, { "amp_sustain", 0.6f, 0.9f },
              { "amp_attack", 5.0f, 15.0f }, { "amp_release", 150.0f, 300.0f, true },
              { "flt_cutoff", 300.0f, 1200.0f, true }, { "glide", 0.0f, 50.0f } },
            9, 2, { "tender", "warm" } }, 0.3f);
    mood ({ "tender.pad", "texture", { "Pad" },
            { { "play_mode", 1 }, { "fm_pitch", -12.0f }, { "fm_algo", 7 }, { "op1_wave", 0.25f }, { "op2_wave", 0.25f },
              { "op3_wave", 0.25f }, { "op4_wave", 0.25f }, { "op1_level", 0.4f }, { "op2_level", 0.4f }, { "op3_ratio", 1.4983f },
              { "op3_level", 0.3f }, { "op4_ratio", 2.0f }, { "op4_level", 0.2f }, { "amp_sustain", 0.9f } },
            { { "op2_ratio", 1.002f, 1.005f }, { "amp_attack", 200.0f, 600.0f, true }, { "amp_release", 800.0f, 1600.0f, true },
              { "flt_cutoff", 1000.0f, 2500.0f, true } },
            9, 2, { "tender", "warm" } }, 0.3f);
    // Melodic recipes: pitched on a C (see Archetype::keyed).
    for (auto& x : a)
        for (const auto* id : { "bass.pluck", "bass.stab", "texture.chord", "texture.drone", "perc.blip", "perc.zap", "perc.zaphard",
                                "synth.zap", "neon.bell", "neon.chord", "neon.pluck", "tender.marimba", "tender.kalimba" })
            if (x.id == id)
                x.keyed = true;
    return a;
}

uint32_t hashOf (const std::string& s)
{
    uint32_t h = 2166136261u;
    for (auto c : s)
        h = (h ^ (uint8_t) c) * 16777619u;
    return h;
}
} // namespace

const std::vector<Archetype>& archetypes()
{
    static const auto list = build();
    return list;
}

const Archetype* findArchetype (const std::string& id)
{
    for (const auto& a : archetypes())
        if (a.id == id)
            return &a;
    return nullptr;
}

const juce::StringArray& moodTags()
{
    static const juce::StringArray tags { "neutral", "synth-aggressive", "neon", "tender" };
    return tags;
}

float loudnessTarget (const std::string& id)
{
    // K-weighted, the loudest 50 ms, dry. Measured against the neutral kit
    // (kick -4, snare -9, tom -9, bass -8, open hat -13, closed hat -20),
    // with claps, rims and hats brought up a little: the neutral clap is
    // quieter than it should be.
    static const std::map<std::string, float> byId {
        { "snare.clap", -12.0f }, { "snare.clapstack", -12.0f }, { "snare.rim", -14.0f }, { "snare.snap", -12.0f },
        { "hat.open", -15.0f }, { "hat.ride", -16.0f }, { "hat.crash", -15.0f }, { "hat.shaker", -17.0f },
        { "hat.tick", -19.0f }, { "texture.loop", -8.0f }, { "texture.crackle", -22.0f }, { "texture.bed", -20.0f },
        { "texture.chord", -12.0f }, { "bass.stab", -11.0f }, { "fx.riser", -14.0f },
    };
    if (const auto it = byId.find (id); it != byId.end())
        return it->second;
    const auto* archetype = findArchetype (id);
    const auto category = archetype != nullptr ? archetype->category : id.substr (0, id.find ('.'));
    if (category == "kick") return -4.5f;
    if (category == "bass") return -8.0f;
    if (category == "snare") return -9.0f;
    if (category == "perc") return -10.0f;
    if (category == "hat") return -18.0f;
    if (category == "fx") return -12.0f;
    return -14.0f; // texture
}

bool fitsCategory (const Archetype& a, const Features& f, juce::String* why)
{
    auto fail = [why] (const char* reason) { if (why != nullptr) *why = reason; return false; };
    const auto& c = a.category;
    if (c == "kick" && f.low < 0.35f) return fail ("no low end");
    if (c == "kick" && f.lengthMs < 80.0f) return fail ("too short for a kick");
    if (c == "snare" && f.centroidHz < 350.0f) return fail ("too boomy for a snare");
    if (c == "hat" && (f.low > 0.05f || f.centroidHz < 2500.0f)) return fail ("too low for a hat");
    if ((a.id == "hat.closed" || a.id == "hat.tick" || a.id == "hat.bit" || a.id == "hat.metal") && f.lengthMs > 400.0f)
        return fail ("too long for a closed hat");
    if ((a.id == "hat.open" || a.id == "hat.ride" || a.id == "hat.crash") && f.lengthMs < 200.0f)
        return fail ("too short for an open hat");
    if (c == "perc" && f.lengthMs < 15.0f) return fail ("too short");
    if (c == "bass" && f.centroidHz > 3000.0f) return fail ("too bright for a bass");
    if (c == "fx" && f.lengthMs < 80.0f) return fail ("too short for FX");
    if (c == "texture" && f.lengthMs < 150.0f) return fail ("too short for a texture");
    if (f.flatness > a.maxFlatness) return fail ("too noisy for a tonal recipe");
    return true;
}

juce::StringArray guessTags (const Archetype& a, const VoiceParams& p, const Features& f)
{
    juce::StringArray tags;
    for (const auto& h : a.hints)
        tags.addIfNotAlreadyThere (h);

    const auto driveType = p.choice (vp::DriveType);
    const auto fmUsed = p.choice (vp::SrcMode) != 1;
    if (driveType == 3 && p[vp::Drive] > 0.3f)
        tags.addIfNotAlreadyThere ("digital");
    if ((driveType == 1 || driveType == 2) && p[vp::Drive] > 0.45f)
        tags.addIfNotAlreadyThere ("harsh");
    if (f.inharmonic > 0.4f && f.flatness < 0.35f && f.centroidHz > 600.0f)
        tags.addIfNotAlreadyThere ("metallic");
    if (fmUsed && p[vp::FmHarm] > 0.35f)
        tags.addIfNotAlreadyThere ("metallic");
    if (f.centroidHz > 6000.0f && f.flatness > 0.45f && a.category != "hat" && a.category != "fx")
        tags.addIfNotAlreadyThere ("harsh");
    // Every sound gets a character (a mood tag isn't one).
    auto hasCharacter = [&] { for (const auto& t : tags) if (characterTags().contains (t)) return true; return false; };
    if (! hasCharacter() && f.centroidHz < 1200.0f && p[vp::Drive] < 0.3f)
        tags.add ("warm");
    if (! hasCharacter())
        tags.add (f.inharmonic > 0.3f ? "metallic" : "organic");

    // Opposites can't both hold; the recipe's own aim wins.
    if (tags.contains ("warm") && tags.contains ("harsh"))
        tags.removeString (a.hints.empty() || a.hints.front() != "warm" ? "warm" : "harsh");
    while (tags.size() > 2)
        tags.remove (tags.size() - 1);
    return tags;
}

// Candidates ------------------------------------------------------------------------

constexpr float kPeakCeilingDb = 1.5f;  // for sorting out duds (as the recipes were first tuned)
constexpr float kFinalCeilingDb = 3.0f;  // the shipped level: a little more headroom for spiky hits

// The final level. A sound that still can't reach its loudness under the
// ceiling (a spiky kick, a thin clap) gets punch: soft saturation in its
// Drive stage, just enough to get there (at most +0.35). Sounds that already
// use Hard, Fold or Crush keep their drive as it is.
void finishLevel (Candidate& c, float target)
{
    const auto* sample = c.sample != nullptr ? c.sample->getDisplayData() : nullptr;
    const auto& levelSpec = voiceParamSpecs()[(size_t) vp::Level];
    auto levelled = [&] (VoiceParams& p)
    {
        auto m = p;
        m[vp::SmpSliceMode] = 0.0f; // a sliced loop is measured whole
        auto f = analyse (m, sample);
        auto level = p[vp::Level] + std::min (target - f.loudnessDb, kFinalCeilingDb - f.peakDb);
        if (level > levelSpec.max && sample != nullptr)
        {
            p[vp::SmpGain] = std::min (24.0f, p[vp::SmpGain] + level - levelSpec.max);
            level = levelSpec.max;
        }
        p[vp::Level] = std::clamp (level, levelSpec.min, levelSpec.max);
        m[vp::Level] = p[vp::Level];
        m[vp::SmpGain] = p[vp::SmpGain];
        return analyse (m, sample);
    };

    auto p = c.params;
    auto f = levelled (p);
    const auto soft = p.choice (vp::DriveType) == 0 || p[vp::Drive] < 0.001f;
    if (target - f.loudnessDb > 2.0f && soft)
    {
        const auto d0 = p[vp::Drive];
        auto lo = d0, hi = std::min (1.0f, d0 + 0.35f);
        auto tryDrive = [&] (float d, VoiceParams& q)
        {
            q = c.params;
            q[vp::Drive] = d;
            q[vp::DriveType] = 0.0f;
            return levelled (q);
        };
        VoiceParams q;
        auto fq = tryDrive (hi, q);
        if (target - fq.loudnessDb <= 1.5f)
            for (int i = 0; i < 6; ++i) // the least drive that gets there
            {
                VoiceParams m;
                const auto mid = 0.5f * (lo + hi);
                const auto fm = tryDrive (mid, m);
                if (target - fm.loudnessDb <= 1.5f) { hi = mid; q = m; fq = fm; }
                else lo = mid;
            }
        if (fq.loudnessDb > f.loudnessDb + 1.0f && fq.finite)
        {
            p = q;
            f = fq;
        }
    }
    c.params = p;
    c.features = f;
}

std::vector<Candidate> makeCandidates()
{
    std::vector<Candidate> all;
    const auto& levelSpec = voiceParamSpecs()[(size_t) vp::Level];

    for (const auto& a : archetypes())
    {
        std::vector<Candidate> made;
        for (int i = 0; i < a.candidates; ++i)
        {
            Candidate c;
            c.archetype = a.id;
            c.index = i;
            c.seed = hashOf (a.id) + (uint32_t) i * 7919u;
            juce::Random r ((juce::int64) c.seed);
            c.params = voiceFrom (a.base);
            for (const auto& range : a.ranges)
            {
                const auto v = range.log ? logBetween (r, range.lo, range.hi) : between (r, range.lo, range.hi);
                apply (c.params, { { range.key, v } });
            }
            if (a.keyed) // on the nearest C, so it plays in tune with the rest
                c.params[vp::FmPitch] = 12.0f * std::floor (c.params[vp::FmPitch] / 12.0f + 0.5f);
            for (int k = 0; k < kNumVoiceParams; ++k) // a range over a menu lands on one of its choices
                if (voiceParamSpecs()[(size_t) k].kind != Kind::Float)
                    c.params[k] = std::round (c.params[k]);

            if (a.sample)
            {
                c.sampleAudio = a.sample (c.seed);
                c.sample = std::make_shared<SampleSlot>();
                auto data = std::make_unique<SampleData>();
                data->audio = c.sampleAudio;
                data->sampleRate = kRate;
                c.sample->setData (std::move (data));
                if (a.loop)
                {
                    c.params[vp::SmpEnd] = 1.0f;
                    c.params[vp::SmpStart] = 0.0f;
                }
            }

            const auto* sample = c.sample != nullptr ? c.sample->getDisplayData() : nullptr;
            // A sliced loop is measured whole (a hit plays one slice).
            auto measured = c.params;
            measured[vp::SmpSliceMode] = 0.0f;
            auto f = analyse (measured, sample);
            c.raw = f;
            if (! f.finite || f.peakDb < -45.0f)
            {
                c.dropped = "silent or broken";
                made.push_back (std::move (c));
                continue;
            }

            // Level to the recipe's loudness; a very short hit sits a little lower.
            // Peaks stay below +1.5 dB, which can leave a spiky sound a bit quieter.
            auto target = loudnessTarget (a.id) + a.hotDb;
            if (f.lengthMs < 50.0f)
                target -= 5.0f * std::log10 (50.0f / std::max (5.0f, f.lengthMs));
            auto gain = std::min (target - f.loudnessDb, kPeakCeilingDb - f.peakDb);
            auto level = c.params[vp::Level] + gain;
            if (level > levelSpec.max && sample != nullptr)
            {
                c.params[vp::SmpGain] = std::min (24.0f, c.params[vp::SmpGain] + level - levelSpec.max);
                level = levelSpec.max;
            }
            c.params[vp::Level] = std::clamp (level, levelSpec.min, levelSpec.max);
            measured[vp::Level] = c.params[vp::Level];
            measured[vp::SmpGain] = c.params[vp::SmpGain];
            f = analyse (measured, sample); // measured again, as it will play
            c.features = f;

            juce::String why;
            if (f.loudnessDb < target - 10.0f)
                c.dropped = f.peakDb > kPeakCeilingDb - 1.0f ? "too spiky to level" : "too quiet to level";
            else if (! fitsCategory (a, f, &why))
                c.dropped = why;
            else
                for (const auto& other : made)
                    if (other.dropped.isEmpty() && distance (other.features, f) < 0.35f)
                    {
                        c.dropped = "near-duplicate";
                        break;
                    }
            c.tags = guessTags (a, c.params, f);
            made.push_back (std::move (c));
        }

        // Auto-pick order: the most typical first, then farthest-first, so the
        // factory's picks spread over what the recipe can do.
        std::vector<Candidate*> ok;
        for (auto& c : made)
            if (c.dropped.isEmpty())
                ok.push_back (&c);
        std::vector<Candidate*> order;
        if (! ok.empty())
        {
            size_t typical = 0;
            float best = 1.0e9f;
            for (size_t i = 0; i < ok.size(); ++i)
            {
                float sum = 0.0f;
                for (auto* o : ok)
                    sum += distance (ok[i]->features, o->features);
                if (sum < best)
                {
                    best = sum;
                    typical = i;
                }
            }
            order.push_back (ok[typical]);
            ok.erase (ok.begin() + (long) typical);
            while (! ok.empty())
            {
                size_t far = 0;
                float farthest = -1.0f;
                for (size_t i = 0; i < ok.size(); ++i)
                {
                    float nearest = 1.0e9f;
                    for (auto* o : order)
                        nearest = std::min (nearest, distance (ok[i]->features, o->features));
                    if (nearest > farthest)
                    {
                        farthest = nearest;
                        far = i;
                    }
                }
                order.push_back (ok[far]);
                ok.erase (ok.begin() + (long) far);
            }
        }
        for (size_t i = 0; i < order.size(); ++i)
            order[i]->score = (float) (order.size() - i);

        // The final level (and punch), after the picks: they go by the shape.
        for (auto* c : order)
        {
            c->shape = c->features;
            auto target = loudnessTarget (a.id) + a.hotDb;
            if (c->shape.lengthMs < 50.0f)
                target -= 5.0f * std::log10 (50.0f / std::max (5.0f, c->shape.lengthMs));
            finishLevel (*c, target);
        }

        for (auto& c : made)
            all.push_back (std::move (c));
    }
    return all;
}

// Names ---------------------------------------------------------------------------------

void nameCandidates (std::vector<Candidate*>& list, const std::map<juce::String, juce::String>& fixed)
{
    // Medians per category, to say what's unusual about each sound.
    std::map<std::string, std::vector<float>> lengths, brightness;
    for (auto* c : list)
    {
        const auto& cat = findArchetype (c->archetype)->category;
        lengths[cat].push_back (c->shape.lengthMs);
        brightness[cat].push_back (c->shape.centroidHz);
    }
    auto median = [] (std::vector<float> v) { std::sort (v.begin(), v.end()); return v.empty() ? 0.0f : v[v.size() / 2]; };
    std::map<std::string, float> medLength, medBright;
    for (auto& [cat, v] : lengths) medLength[cat] = median (v);
    for (auto& [cat, v] : brightness) medBright[cat] = median (v);

    // Descriptors (no kit names among them, so a sound never reads as a kit).
    const std::map<juce::String, std::vector<const char*>> byTag {
        { "neutral", { "Clean", "Even", "Pure", "Simple", "Level", "Steady", "Balanced" } },
        { "synth-aggressive", { "Saw", "Pulse", "Square", "Synth", "Laser", "Fuzz", "Razor" } },
        { "neon", { "Neon", "Prism", "Chroma", "Lumen", "Halo", "Flash", "Beam" } },
        { "tender", { "Tender", "Feather", "Gentle", "Cotton", "Silk", "Velour", "Mellow" } },
        { "warm", { "Warm", "Tape", "Round", "Soft", "Mellow", "Amber", "Plush" } },
        { "harsh", { "Raw", "Burnt", "Hard", "Rough", "Harsh", "Jagged", "Scorched" } },
        { "digital", { "Bit", "Grain", "Pixel", "Byte", "Data", "Lo-Res", "Stepped" } },
        { "metallic", { "Steel", "Iron", "Chrome", "Tin", "Brass", "Copper", "Zinc" } },
        { "organic", { "Dry", "Wood", "Skin", "Plain", "Paper", "Hollow", "Felt" } },
    };
    const std::vector<const char*> shortWords { "Short", "Tight", "Clipped", "Small", "Quick", "Brief", "Choked" };
    const std::vector<const char*> longWords { "Long", "Deep", "Wide", "Slow", "Big", "Full", "Heavy" };
    const std::vector<const char*> brightWords { "Bright", "Glass", "Crisp", "Thin", "Sharp", "Airy", "Clear" };
    const std::vector<const char*> darkWords { "Dark", "Dull", "Low", "Muted", "Smoky", "Dusk", "Murky" };

    std::map<std::string, std::set<juce::String>> used; // per category
    // The neutral kit's sound names are taken.
    used["kick"] = { "Kick" }; used["snare"] = { "Rim", "Snare", "Clap" }; used["perc"] = { "Tom" };
    used["bass"] = { "Bass" }; used["hat"] = { "Closed Hat", "Open Hat" };

    // Names given before stay.
    std::set<Candidate*> named;
    for (auto* c : list)
        if (const auto it = fixed.find (juce::String (c->archetype) + "#" + juce::String (c->index)); it != fixed.end())
        {
            const auto& cat = findArchetype (c->archetype)->category;
            if (! used[cat].count (it->second))
            {
                c->name = it->second;
                used[cat].insert (c->name);
                named.insert (c);
            }
        }

    for (auto* c : list)
    {
        if (named.count (c))
            continue;
        const auto& a = *findArchetype (c->archetype);
        const auto len = std::log2 ((c->shape.lengthMs + 20.0f) / (medLength[a.category] + 20.0f));
        const auto bright = std::log2 ((c->shape.centroidHz + 50.0f) / (medBright[a.category] + 50.0f));

        // Descriptors, most telling first.
        std::vector<std::pair<float, const std::vector<const char*>*>> axes;
        std::vector<std::vector<const char*>> tagWords;
        for (const auto& t : c->tags)
            if (auto it = byTag.find (t); it != byTag.end())
                tagWords.push_back (it->second);
        for (size_t i = 0; i < tagWords.size(); ++i)
            axes.push_back ({ 0.6f - 0.1f * (float) i, &tagWords[i] });
        axes.push_back ({ std::abs (len), len < 0 ? &shortWords : &longWords });
        axes.push_back ({ std::abs (bright), bright > 0 ? &brightWords : &darkWords });
        std::stable_sort (axes.begin(), axes.end(), [] (const auto& x, const auto& y) { return x.first > y.first; });

        juce::String name;
        for (int round = 0; round < 7 && name.isEmpty(); ++round)
            for (const auto& [weight, words] : axes)
            {
                for (const auto& noun : a.nouns)
                {
                    const auto candidate = juce::String (words->at ((size_t) round % words->size())) + " " + noun;
                    if (! used[a.category].count (candidate) && ! juce::String (noun).startsWith (words->at ((size_t) round % words->size())))
                    {
                        name = candidate;
                        break;
                    }
                }
                if (name.isNotEmpty())
                    break;
            }
        // Still taken: two descriptors ("Dark Tight Snare").
        for (size_t i = 0; i < axes.size() && name.isEmpty(); ++i)
            for (size_t j = i + 1; j < axes.size() && name.isEmpty(); ++j)
                for (size_t w = 0; w < 7 && name.isEmpty(); ++w)
                {
                    const auto candidate = juce::String (axes[i].second->at (w % axes[i].second->size())) + " "
                                         + axes[j].second->at (w % axes[j].second->size()) + " " + a.nouns.front();
                    if (! used[a.category].count (candidate))
                        name = candidate;
                }
        for (int n = 2; name.isEmpty(); ++n)
            if (const auto candidate = juce::String (a.nouns.front()) + " " + juce::String (n); ! used[a.category].count (candidate))
                name = candidate;
        used[a.category].insert (name);
        c->name = name;
    }
}

} // namespace batida::factory
