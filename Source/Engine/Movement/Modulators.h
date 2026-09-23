#pragma once

#include "Movement.h"

#include <atomic>

namespace batida
{

enum class ModulatorMode { Sync, Free, OneShot };

// Length of each Sync rate, in quarter notes (matches the Mod Rate choices).
double syncRateQuarters (int rateIndex);

// The two modulators at control rate. Sync follows the clock the kit gives
// it (the host position, or the internal tempo); Free runs in Hz; One-shot
// runs once per trigger and holds its last value.
class Modulators
{
public:
    void prepare (double sampleRate);
    void reset();

    // Triggers (for One-shot): a voice hit, or a pattern starting its cycle.
    void hit (int voice);
    void patternStart();

    // Advances by `numSamples` from `ppq` (quarter notes) at `ppqPerSample`
    // and updates the values.
    void advance (int numSamples, double ppq, double ppqPerSample, const std::array<float, kNumGlobalParams>& globals,
                  const MovementData& data);

    float value (int mod) const { return values[(size_t) mod]; }

    // For the UI (any thread).
    float getUiValue (int mod) const { return uiValue[(size_t) mod].load(); }
    float getUiPhase (int mod) const { return uiPhase[(size_t) mod].load(); }

private:
    double sampleRate = 48000.0;
    std::array<double, kNumMods> freePhase {}, shotPhase {};
    std::array<bool, kNumMods> shotRunning {}, triggered {};
    std::array<float, kNumMods> values {}, smoothed {}, drift {};
    std::array<double, kNumMods> lastPhase {};
    bool pendingPatternStart = false;
    std::array<bool, kNumVoices> pendingHits {};
    juce::Random random { 0x6d6f64 };

    std::array<std::atomic<float>, kNumMods> uiValue {}, uiPhase {};
};

} // namespace batida
