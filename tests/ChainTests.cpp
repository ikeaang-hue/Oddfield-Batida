#include "Tools.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace batida
{

namespace
{
constexpr double kRate = 48000.0;

// Default kit with the chain set to neutral: no heat, nothing added.
KitParams neutralKit()
{
    auto kit = defaultKitParams();
    kit.global[gp::XyY] = 0.0f;
    kit.global[gp::SafetyClip] = 0.0f;
    return kit;
}

float rmsDb (const juce::AudioBuffer<float>& b)
{
    double sum = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
        sum += (double) b.getSample (0, i) * b.getSample (0, i);
    return (float) (10.0 * std::log10 (std::max (1.0e-20, sum / b.getNumSamples())));
}

float maxDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    float d = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            d = std::max (d, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return d;
}

// Energy of (b - a) relative to a, in dB.
float errorDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    double sig = 0.0, err = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
        {
            const auto x = a.getSample (ch, i), d = b.getSample (ch, i) - x;
            sig += (double) x * x;
            err += (double) d * d;
        }
    return (float) (10.0 * std::log10 (std::max (err, 1.0e-30) / std::max (sig, 1.0e-30)));
}

void setChainAmount (KitParams& kit, float amount)
{
    for (auto& v : kit.voices)
        v[vp::ChainAmt] = amount;
}
} // namespace

class ChainTests final : public juce::UnitTest
{
public:
    ChainTests() : juce::UnitTest ("Kit chain", "Batida") {}

    // Alias energy relative to the fundamental, in dB, for a bin-aligned sine
    // through `process` (one sample in, one out).
    template <typename Fn>
    static float aliasDb (Fn&& process)
    {
        constexpr int order = 13, n = 1 << order, bin = 840; // 4921.875 Hz at 48 kHz
        const auto hz = kRate * bin / n;
        std::vector<float> out ((size_t) (2 * n), 0.0f);
        for (int i = 0; i < 2 * n; ++i)
            out[(size_t) i] = process ((float) (0.5 * std::sin (2.0 * 3.141592653589793 * hz * i / kRate)));

        std::vector<float> fft ((size_t) (2 * n), 0.0f);
        for (int i = 0; i < n; ++i)
        {
            const auto w = 0.35875 - 0.48829 * std::cos (2 * 3.141592653589793 * i / n)
                         + 0.14128 * std::cos (4 * 3.141592653589793 * i / n) - 0.01168 * std::cos (6 * 3.141592653589793 * i / n);
            fft[(size_t) i] = out[(size_t) (n + i)] * (float) w; // second half: settled
        }
        juce::dsp::FFT (order).performFrequencyOnlyForwardTransform (fft.data());

        double fundamental = 0.0, alias = 0.0;
        for (int k = 8; k < n / 2; ++k)
        {
            const auto e = (double) fft[(size_t) k] * fft[(size_t) k];
            const auto nearest = (int) std::lround ((double) k / bin) * bin;
            if (std::abs (k - bin) <= 4)
                fundamental += e;
            else if (std::abs (k - nearest) > 4 || nearest == 0)
                alias += e;
        }
        return (float) (10.0 * std::log10 (std::max (alias, 1.0e-30) / fundamental));
    }

    void runTest() override
    {
        beginTest ("Crossover: bands sum flat, equal the dry allpass, and stay separate");
        for (const auto [hz, lowMaxDb, highMaxDb] : { std::tuple { 9.0, 0.1, -70.0 }, std::tuple { 90.0, -5.9, -5.9 },
                                                      std::tuple { 900.0, -70.0, 0.1 }, std::tuple { 9000.0, -100.0, 0.1 } })
        {
            SvfSection lp1, lp2, hp1, hp2, ap;
            for (auto* s : { &lp1, &lp2, &hp1, &hp2, &ap })
                s->setCutoff (90.0, kRate);
            float sumPeak = 0.0f, lowPeak = 0.0f, highPeak = 0.0f, apDiff = 0.0f;
            for (int i = 0; i < 96000; ++i)
            {
                const auto x = (float) std::sin (2.0 * 3.141592653589793 * hz * i / kRate);
                lp1.tick (x);
                lp2.tick (lp1.lp);
                hp1.tick (x);
                hp2.tick (hp1.hp);
                ap.tick (x);
                if (i > 48000)
                {
                    sumPeak = std::max (sumPeak, std::abs (lp2.lp + hp2.hp));
                    lowPeak = std::max (lowPeak, std::abs (lp2.lp));
                    highPeak = std::max (highPeak, std::abs (hp2.hp));
                    apDiff = std::max (apDiff, std::abs (lp2.lp + hp2.hp - ap.allpass (x)));
                }
            }
            const juce::String at = " at " + juce::String (hz) + " Hz";
            expectWithinAbsoluteError (20.0f * std::log10 (sumPeak), 0.0f, 0.1f, "sum" + at);
            expectLessThan (apDiff, 1.0e-3f, "allpass" + at);
            expectLessThan (20.0f * std::log10 (lowPeak), (float) lowMaxDb + 0.2f, "low band" + at);
            expectLessThan (20.0f * std::log10 (highPeak), (float) highMaxDb + 0.2f, "high band" + at);
        }

        beginTest ("At Chain amount 0 the chain changes neither level nor tone");
        {
            auto a = neutralKit();
            setChainAmount (a, 0.0f);
            auto b = a;
            b.global[gp::XyX] = 1.0f;
            b.global[gp::XyY] = 1.0f;
            b.global[gp::CompAmount] = 1.0f;
            b.global[gp::EqLow] = 12.0f;
            b.global[gp::EqHigh] = -12.0f;
            b.global[gp::ChainOut] = 12.0f;
            const auto ra = tools::renderVoice (a, 0, 60, 1.0f, 1.2);
            const auto rb = tools::renderVoice (b, 0, 60, 1.0f, 1.2);
            // Only the dry allpass follows the pad's Low Keep: phase, not level.
            logMessage ("  kick at 0%, neutral vs extreme chain: " + juce::String (rmsDb (ra), 3) + " / "
                        + juce::String (rmsDb (rb), 3) + " dB");
            expectWithinAbsoluteError (rmsDb (rb), rmsDb (ra), 0.05f);
            b.global[gp::DistFollow] = 0.0f; // Low Keep stays put → identical
            expectLessThan (maxDiff (ra, tools::renderVoice (b, 0, 60, 1.0f, 1.2)), 1.0e-6f);
        }

        beginTest ("Dry and wet line up: 0%, 50% and 100% through a neutral chain match");
        {
            auto k0 = neutralKit();
            setChainAmount (k0, 0.0f);
            auto k50 = neutralKit();
            setChainAmount (k50, 0.5f);
            auto k100 = neutralKit();
            setChainAmount (k100, 1.0f);
            const auto r0 = tools::renderVoice (k0, 0, 60, 1.0f, 0.5);
            const auto r50 = tools::renderVoice (k50, 0, 60, 1.0f, 0.5);
            const auto r100 = tools::renderVoice (k100, 0, 60, 1.0f, 0.5);
            logMessage ("  kick RMS 0/50/100%: " + juce::String (rmsDb (r0), 2) + " / " + juce::String (rmsDb (r50), 2)
                        + " / " + juce::String (rmsDb (r100), 2) + " dB");
            // Only the oversampling filters touch the undistorted wet path; the
            // difference must stay far below the signal (no audible comb).
            const auto e50 = errorDb (r0, r50), e100 = errorDb (r0, r100);
            logMessage ("  difference vs dry at 50% / 100%: " + juce::String (e50, 1) + " / " + juce::String (e100, 1) + " dB");
            expectLessThan (e50, -40.0f);
            expectLessThan (e100, -34.0f);
        }

        beginTest ("Compressor static curve");
        expectWithinAbsoluteError (Compressor::staticGainDb (0.0f, 1.0f), -32.4f, 0.05f); // 36 dB over at 10:1
        expectWithinAbsoluteError (Compressor::staticGainDb (-50.0f, 1.0f), 0.0f, 1.0e-6f);
        expectWithinAbsoluteError (Compressor::staticGainDb (0.0f, 0.0f), 0.0f, 1.0e-6f);

        beginTest ("The sidechain input keys the compressor");
        {
            auto kit = neutralKit();
            kit.global[gp::CompAmount] = 0.8f;
            kit.global[gp::CompDetector] = 1.0f;

            auto render = [&] (bool key)
            {
                Kit k;
                k.prepare (kRate, 256);
                juce::AudioBuffer<float> buf (2, 256), sc (2, 256), out (2, 24000);
                for (int pos = 0; pos < 24000; pos += 256)
                {
                    sc.clear();
                    if (key)
                        for (int i = 0; i < 256; ++i)
                            sc.setSample (0, i, (float) std::sin (2.0 * 3.141592653589793 * 100.0 * (pos + i) / kRate));
                    k.setParameters (kit);
                    if (pos == 0)
                        k.noteOn (5, 60, 1.0f); // bass, Gate mode, held
                    k.process (buf, {}, &sc);
                    out.copyFrom (0, pos, buf, 0, 0, std::min (256, 24000 - pos));
                }
                return rmsDb (out);
            };
            const auto open = render (false), keyed = render (true);
            logMessage ("  bass RMS without / with key: " + juce::String (open, 1) + " / " + juce::String (keyed, 1) + " dB");
            expectLessThan (keyed, open - 6.0f);
            expectLessThan (open, 0.0f, "no make-up boost while the key is silent");
        }

        beginTest ("XY offsets: Follow off leaves the knobs; Follow on adds heat");
        {
            auto g = defaultKitParams().global;
            g[gp::XyX] = 1.0f;
            g[gp::XyY] = 1.0f;
            g[gp::DistDrive] = 0.1f;
            g[gp::DistType] = 0.3f;
            g[gp::EqHigh] = 2.0f;

            auto on = computeEffectiveChain (g);
            expectWithinAbsoluteError (on.drive, 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (on.type, 0.8f, 1.0e-6f);
            expectWithinAbsoluteError (on.compAmount, 0.25f, 1.0e-6f, "heat adds only a little compression");
            expectGreaterThan (on.compAttack, 10.0f, "and a longer attack, so transients punch through");
            expectWithinAbsoluteError (on.lowKeepHz, 90.0f * std::sqrt (2.0f), 0.01f, "the crossover rises only a little");
            expectGreaterThan (on.outGain, 1.3f, "heat lifts the output (crushed, not muted)");
            expectWithinAbsoluteError (on.highDb, -3.0f, 1.0e-4f);

            g[gp::CompFollow] = g[gp::DistFollow] = g[gp::EqFollow] = 0.0f;
            const auto off = computeEffectiveChain (g);
            expectWithinAbsoluteError (off.drive, 0.1f, 1.0e-6f);
            expectWithinAbsoluteError (off.type, 0.3f, 1.0e-6f);
            expectWithinAbsoluteError (off.compAmount, 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (off.highDb, 2.0f, 1.0e-6f);
            expectWithinAbsoluteError (off.lowKeepHz, 90.0f, 1.0e-3f);
            expectWithinAbsoluteError (off.outGain, 1.0f, 1.0e-6f);
        }

        beginTest ("Heat crushes rather than mutes: a kick keeps its loudness");
        {
            // The default kick on its own, through the chain, clean and at full heat.
            auto params = defaultKitParams();
            params.global[gp::XyX] = 0.5f;
            auto loudness = [&] (float heat)
            {
                params.global[gp::XyY] = heat;
                const auto audio = tools::renderVoice (params, 0, kDrumMapFirstNote, 0.9f, 1.0, kRate);
                return tools::measure (audio, kRate).rmsDb;
            };
            const auto clean = loudness (0.0f), destroyed = loudness (1.0f);
            logMessage ("  kick RMS clean / full heat: " + juce::String (clean, 1) + " / " + juce::String (destroyed, 1) + " dB");
            expectGreaterThan (destroyed, clean - 1.5f);
        }

        beginTest ("4x oversampling reduces aliasing in the distortion");
        for (const auto [driveAmount, typeAmount, name] : { std::tuple { 1.0f, 0.5f, "full clip" },
                                                            std::tuple { 0.5f, 0.5f, "half clip" },
                                                            std::tuple { 0.6f, 0.0f, "tape" },
                                                            std::tuple { 0.6f, 0.75f, "fold" } })
        {
            ChainSettings s;
            s.drive = driveAmount;
            s.type = typeAmount;
            s.lowKeepHz = 40.0f;
            KitChain chain;
            chain.setSettings (s);
            chain.prepare (kRate, 1);
            const auto oversampled = aliasDb ([&] (float x)
            {
                float l = 0.0f, r = 0.0f;
                chain.process (&l, &r, &x, &x, nullptr, nullptr, 1);
                return l;
            });

            Saturator plain;
            const auto direct = aliasDb ([&] (float x) { return plain.process (x, driveAmount, typeAmount, 0); });
            logMessage ("  " + juce::String (name) + ": alias " + juce::String (oversampled, 1) + " dB vs "
                        + juce::String (direct, 1) + " dB without oversampling");
            expectLessThan (oversampled, direct - 10.0f, name);
        }

        beginTest ("Mute and solo");
        {
            auto peakOf = [] (const KitParams& k, int voice)
            {
                return tools::measure (tools::renderVoice (k, voice, 60, 1.0f, 0.3), kRate).peakDb;
            };
            auto kit = defaultKitParams();
            const auto open = peakOf (kit, 0);

            auto muted = kit;
            muted.voices[0][vp::Mute] = 1.0f;
            expectLessThan (peakOf (muted, 0), -100.0f, "a muted voice is silent");

            auto otherSolo = kit;
            otherSolo.voices[2][vp::Solo] = 1.0f;
            expectLessThan (peakOf (otherSolo, 0), -100.0f, "while another voice is soloed");

            auto ownSolo = otherSolo;
            ownSolo.voices[0][vp::Solo] = 1.0f;
            expectWithinAbsoluteError (peakOf (ownSolo, 0), open, 0.01f, "soloed voices still sound");

            // Muting mid-note fades out instead of clicking.
            Kit k;
            k.setParameters (kit);
            k.prepare (kRate, 64);
            juce::AudioBuffer<float> buf (2, 64);
            k.noteOn (5, 60, 1.0f); // bass, sustained
            float lastPeak = 0.0f, maxStep = 0.0f, prev = 0.0f;
            for (int b = 0; b < 100; ++b) // mute at block 20 (27 ms in), measure ~100 ms later (the crossover rings out)
            {
                auto p = kit;
                if (b >= 20)
                    p.voices[5][vp::Mute] = 1.0f;
                k.setParameters (p);
                k.process (buf, {});
                for (int i = 0; i < 64; ++i)
                {
                    const auto x = buf.getSample (0, i);
                    if (b >= 20)
                        maxStep = std::max (maxStep, std::abs (x - prev));
                    prev = x;
                }
                lastPeak = buf.getMagnitude (0, 0, 64);
            }
            expectLessThan (lastPeak, 1.0e-4f, "silent after the fade");
            expectLessThan (maxStep, 0.05f, "no click when muting");
        }

        beginTest ("Every pad corner stays finite");
        for (const auto [x, y] : { std::pair { 0.0f, 0.0f }, std::pair { 1.0f, 0.0f }, std::pair { 0.5f, 0.5f },
                                   std::pair { 0.0f, 1.0f }, std::pair { 1.0f, 1.0f } })
        {
            auto kit = defaultKitParams();
            kit.global[gp::XyX] = x;
            kit.global[gp::XyY] = y;
            kit.global[gp::ExcAmount] = 1.0f;
            kit.global[gp::CompAmount] = 1.0f;
            for (int v = 0; v < kNumVoices; ++v)
            {
                const auto s = tools::measure (tools::renderVoice (kit, v, 60, 1.0f, 0.4), kRate);
                expect (s.finite, "voice " + juce::String (v + 1) + " at " + juce::String (x) + "," + juce::String (y));
                expect (s.peakDb <= 0.01f, "safety clip holds the ceiling");
            }
        }
    }
};

static ChainTests chainTests;

} // namespace batida
