#include "KitChain.h"

#include <algorithm>
#include <cmath>

namespace batida
{

namespace
{
constexpr int kFactor = Saturator::kOversampling;

void copyCoefficients (SvfSection& dst, const SvfSection& src)
{
    dst.g = src.g;
    dst.k = src.k;
    dst.a1 = src.a1;
    dst.a2 = src.a2;
    dst.a3 = src.a3;
}
} // namespace

void KitChain::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlock = std::max (1, maxBlockSize);

    oversampler.initProcessing ((size_t) maxBlock);
    latency = (int) std::lround (oversampler.getLatencyInSamples());

    highBand.setSize (2, maxBlock);
    lowBand.setSize (2, maxBlock);
    driveTrack.assign ((size_t) maxBlock, 0.0f);
    typeTrack.assign ((size_t) maxBlock, 0.5f);
    excTrack.assign ((size_t) maxBlock, 0.0f);

    for (auto& d : lowDelay)
        d.setSize (latency);
    for (auto& d : dryDelay)
        d.setSize (latency);

    compressor.prepare (sampleRate);
    for (auto* s : { &compAmount, &compMix, &drive, &type, &excAmount, &logLowKeep, &logExcTone, &outGain })
        s->setTime (20.0f, sampleRate);

    Saturator::compensation (0.0f, 0.0f); // build the loudness table now, not on the audio thread
    reset();
}

void KitChain::reset()
{
    compressor.reset();
    oversampler.reset();
    saturator.reset();
    exciterSaturator.reset();

    for (auto* arr : { &lp1, &lp2, &hp1, &hp2, &dryAllpass, &excHpIn1, &excHpIn2, &excHpOut, &eqHp, &eqLp })
        for (auto& s : *arr)
            s.reset();
    for (auto* arr : { &eqLow, &eqMid, &eqHigh })
        for (auto& b : *arr)
            b.reset();
    for (auto& d : lowDelay)
        d.reset();
    for (auto& d : dryDelay)
        d.reset();
    dcX.fill (0.0f);
    dcY.fill (0.0f);

    compAmount.snap (target.compAmount);
    compMix.snap (target.compMix);
    drive.snap (target.drive);
    type.snap (target.type);
    excAmount.snap (target.excAmount);
    logLowKeep.snap (std::log2 (target.lowKeepHz));
    logExcTone.snap (std::log2 (target.excToneHz));
    outGain.snap (target.outGain);
    crossoverHz = excToneHz = -1.0f;
    eqHpHz = eqLpHz = -1.0f;
    eqLowDb = eqMidDb = eqHighDb = 99.0f;
}

void KitChain::updateCrossover (float hz)
{
    crossoverHz = hz;
    SvfSection proto;
    proto.setCutoff (hz, sampleRate);
    for (int ch = 0; ch < 2; ++ch)
        for (auto* s : { &lp1[(size_t) ch], &lp2[(size_t) ch], &hp1[(size_t) ch], &hp2[(size_t) ch], &dryAllpass[(size_t) ch] })
            copyCoefficients (*s, proto);
}

void KitChain::updateEq()
{
    if (target.hpHz != eqHpHz)
    {
        eqHpHz = target.hpHz;
        for (auto& s : eqHp)
            s.setCutoff (eqHpHz, sampleRate);
    }
    if (target.lpHz != eqLpHz)
    {
        eqLpHz = target.lpHz;
        for (auto& s : eqLp)
            s.setCutoff (eqLpHz, sampleRate);
    }
    if (target.lowDb != eqLowDb)
    {
        eqLowDb = target.lowDb;
        for (auto& b : eqLow)
            b.set (Biquad::Shape::LowShelf, 120.0, sampleRate, eqLowDb, 0.707f);
    }
    if (target.midDb != eqMidDb)
    {
        eqMidDb = target.midDb;
        for (auto& b : eqMid)
            b.set (Biquad::Shape::Bell, 1200.0, sampleRate, eqMidDb, 0.7f);
    }
    if (target.highDb != eqHighDb)
    {
        eqHighDb = target.highDb;
        for (auto& b : eqHigh)
            b.set (Biquad::Shape::HighShelf, 8000.0, sampleRate, eqHighDb, 0.707f);
    }
}

void KitChain::process (float* dryL, float* dryR, const float* wetL, const float* wetR,
                        const float* sidechainL, const float* sidechainR, int numSamples)
{
    jassert (numSamples <= maxBlock);
    compressor.setTimes (target.compAttack, target.compRelease);
    updateEq();

    float* hi[2] = { highBand.getWritePointer (0), highBand.getWritePointer (1) };
    float* lo[2] = { lowBand.getWritePointer (0), lowBand.getWritePointer (1) };
    float* dry[2] = { dryL, dryR };
    const float* wet[2] = { wetL, wetR };
    const bool keyed = target.sidechain && sidechainL != nullptr;

    // 1x: dynamics, crossover, dry alignment -------------------------------
    for (int i = 0; i < numSamples; ++i)
    {
        const auto amount = compAmount.next (target.compAmount);
        const auto mix = compMix.next (target.compMix);
        driveTrack[(size_t) i] = drive.next (target.drive);
        typeTrack[(size_t) i] = type.next (target.type);
        excTrack[(size_t) i] = excAmount.next (target.excAmount);

        const auto lk = logLowKeep.value;
        if (std::abs (logLowKeep.next (std::log2 (target.lowKeepHz)) - lk) > 1.0e-5f || crossoverHz < 0.0f)
            updateCrossover (std::exp2 (logLowKeep.value));

        const auto detector = keyed ? std::max (std::abs (sidechainL[i]), std::abs (sidechainR != nullptr ? sidechainR[i] : sidechainL[i]))
                                    : std::max (std::abs (wetL[i]), std::abs (wetR[i]));
        auto gain = compressor.gainFor (detector, amount);
        const auto inE = wetL[i] * wetL[i] + wetR[i] * wetR[i];
        const auto makeup = compressor.makeupFor (inE, inE * gain * gain);
        if (! keyed) // a key ducks: no make-up
            gain *= makeup;

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto x = wet[ch][i];
            const auto c = x + (x * gain - x) * mix;

            auto& a = lp1[(size_t) ch];
            auto& b = lp2[(size_t) ch];
            a.tick (c);
            b.tick (a.lp);
            auto& h1 = hp1[(size_t) ch];
            auto& h2 = hp2[(size_t) ch];
            h1.tick (c);
            h2.tick (h1.hp);

            lo[ch][i] = lowDelay[(size_t) ch].process (b.lp);
            hi[ch][i] = h2.hp;

            auto& ap = dryAllpass[(size_t) ch];
            ap.tick (dry[ch][i]);
            dry[ch][i] = dryDelay[(size_t) ch].process (ap.allpass (dry[ch][i]));
        }
    }

    // 4x: distortion and exciter on the high band -------------------------
    juce::dsp::AudioBlock<float> block (hi, 2, (size_t) numSamples);
    auto up = oversampler.processSamplesUp (block);
    const auto upRate = sampleRate * kFactor;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto t = std::log2 (target.excToneHz);
        const auto lt = logExcTone.value;
        if (std::abs (logExcTone.next (t) - lt) > 1.0e-4f || excToneHz < 0.0f)
        {
            excToneHz = std::exp2 (logExcTone.value);
            for (int ch = 0; ch < 2; ++ch)
                for (auto* s : { &excHpIn1[(size_t) ch], &excHpIn2[(size_t) ch], &excHpOut[(size_t) ch] })
                    s->setCutoff (excToneHz, upRate);
        }

        const auto d = driveTrack[(size_t) i];
        const auto ty = typeTrack[(size_t) i];
        const auto ex = excTrack[(size_t) i];

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* x = up.getChannelPointer ((size_t) ch) + (size_t) i * kFactor;
            for (int k = 0; k < kFactor; ++k)
            {
                auto y = saturator.process (x[k], d, ty, ch);

                if (ex > 0.0f)
                {
                    // Exciter: the band above Tone, driven through the same shaper,
                    // then only its new harmonics above Tone are added back.
                    auto& e1 = excHpIn1[(size_t) ch];
                    auto& e2 = excHpIn2[(size_t) ch];
                    auto& e3 = excHpOut[(size_t) ch];
                    e1.tick (x[k]);
                    e2.tick (e1.hp);
                    e3.tick (exciterSaturator.process (e2.hp * 4.0f, 0.5f, ty, ch));
                    y += e3.hp * ex * 0.5f;
                }
                x[k] = y;
            }
        }
    }

    oversampler.processSamplesDown (block);

    // 1x: recombine, EQ, output ------------------------------------------
    const bool useHp = target.hpHz > 20.5f;
    const bool useLp = target.lpHz < 19900.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto g = outGain.next (target.outGain);
        for (int ch = 0; ch < 2; ++ch)
        {
            // DC blocker on the distorted band (the tube shape is asymmetric).
            const auto h = hi[ch][i];
            const auto hdc = h - dcX[(size_t) ch] + 0.9995f * dcY[(size_t) ch];
            dcX[(size_t) ch] = h;
            dcY[(size_t) ch] = hdc;

            auto y = lo[ch][i] + hdc;

            if (useHp)
            {
                eqHp[(size_t) ch].tick (y);
                y = eqHp[(size_t) ch].hp;
            }
            if (useLp)
            {
                eqLp[(size_t) ch].tick (y);
                y = eqLp[(size_t) ch].lp;
            }
            if (eqLowDb != 0.0f)
                y = eqLow[(size_t) ch].process (y);
            if (eqMidDb != 0.0f)
                y = eqMid[(size_t) ch].process (y);
            if (eqHighDb != 0.0f)
                y = eqHigh[(size_t) ch].process (y);

            dry[ch][i] += y * g;
        }
    }
}

} // namespace batida
