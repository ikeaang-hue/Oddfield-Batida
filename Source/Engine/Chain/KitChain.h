#pragma once

#include "ChainFilters.h"
#include "ChainSettings.h"
#include "Compressor.h"
#include "Engine/Processors.h"
#include "Saturator.h"

#include <juce_dsp/juce_dsp.h>

namespace batida
{

// The shared kit chain: dynamics → distortion (+ exciter, clean low end) →
// EQ → output, run on the wet bus of all voices.
//
// Distortion and exciter run at 4x on the band above Low Keep. The split is
// Linkwitz-Riley 4th order: steep, so the lows stay truly clean, and the two
// bands sum flat as a 2nd-order allpass. The dry bus gets the same allpass
// and the same latency, so dry and wet line up at any Chain amount (no comb,
// no low-end cancellation). The allpass changes phase only, not level or tone.
class KitChain
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setSettings (const ChainSettings& s) { target = s; }

    int getLatencySamples() const { return latency; }
    float getGainReductionDb() const { return compressor.getGainReductionDb(); }

    // Mixes dry + processed wet into dryL/dryR. sidechainL/R may be null.
    void process (float* dryL, float* dryR, const float* wetL, const float* wetR,
                  const float* sidechainL, const float* sidechainR, int numSamples);

private:
    // Fixed delay of n samples (n = 0 passes straight through).
    struct Delay
    {
        std::vector<float> buffer;
        int pos = 0;
        void setSize (int n) { buffer.assign ((size_t) std::max (0, n), 0.0f); pos = 0; }
        void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; }
        float process (float x)
        {
            if (buffer.empty())
                return x;
            const auto y = buffer[(size_t) pos];
            buffer[(size_t) pos] = x;
            pos = (pos + 1) % (int) buffer.size();
            return y;
        }
    };

    void updateCrossover (float hz);
    void updateEq();

    double sampleRate = 48000.0;
    int maxBlock = 0;
    int latency = 0;
    ChainSettings target;

    Smoother compAmount, compMix, drive, type, excAmount, logLowKeep, logExcTone, outGain;
    float crossoverHz = -1.0f, excToneHz = -1.0f;

    Compressor compressor;

    // Crossover (per channel): two Butterworth sections per band = LR4.
    std::array<SvfSection, 2> lp1, lp2, hp1, hp2, dryAllpass;
    std::array<Delay, 2> lowDelay, dryDelay;

    juce::dsp::Oversampling<float> oversampler { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true };
    juce::AudioBuffer<float> highBand, lowBand;
    std::vector<float> driveTrack, typeTrack, excTrack;

    Saturator saturator, exciterSaturator;
    std::array<SvfSection, 2> excHpIn1, excHpIn2, excHpOut; // at 4x
    std::array<float, 2> dcX {}, dcY {};

    // EQ
    std::array<SvfSection, 2> eqHp, eqLp;
    std::array<Biquad, 2> eqLow, eqMid, eqHigh;
    float eqHpHz = -1.0f, eqLpHz = -1.0f, eqLowDb = 99.0f, eqMidDb = 99.0f, eqHighDb = 99.0f;
};

} // namespace batida
