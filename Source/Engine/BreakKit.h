#pragma once

#include "Parameters.h"
#include "SampleData.h"
#include "Sequencer/Pattern.h"

#include <optional>

namespace batida
{

// Drop a break, get a kit: a drum loop is cut at its hits, each hit is heard
// as a kick, snare, hat or percussion, and the loop goes into those slots in
// Transients slice mode. A pattern plays every hit back from its own slice on
// the step where it fell, so the break replays at any tempo without
// stretching (SPEC §8).
//
// The loop is taken to be half a bar, 1, 2 or 4 bars of 4/4, whichever gives
// the most likely tempo within 40–240 bpm. Slots follow the kit layout: 1 kick, 2 percussion, 3 snare,
// 7 closed hat, 8 open hat (the hats choke each other); the rest are untouched.

enum class HitKind { Kick, Snare, ClosedHat, OpenHat, Perc };

// The loop's tempo must be one the sequencer plays (the Tempo parameter's range).
constexpr double kBreakMinBpm = 40.0, kBreakMaxBpm = 240.0;

struct BreakHit
{
    int frame = 0;  // where the slice starts in the loop
    int slice = 0;  // its index in the slot's Transients slices
    int step = 0;   // the 16th it lands on
    HitKind kind = HitKind::Perc;
};

struct BreakPlan
{
    double bars = 1.0; // 0.5, 1, 2 or 4
    int steps = 16;
    double bpm = 120.0;
    float sensitivity = 1.0f; // Slice Sensitivity that gives exactly these slices
    std::vector<BreakHit> hits;
    std::array<bool, kNumVoices> used {};
    std::array<VoiceParams, kNumVoices> voices {}; // for the used slots
    std::array<const char*, kNumVoices> names {};
    Pattern pattern;
};

// What a hit sounds like: shares of its energy below ~120 Hz, above ~2 kHz
// and above ~6 kHz; how long it rings (to -20 dB); how periodic (tonal) it is.
struct HitFeatures
{
    float low = 0.0f, bright = 0.0f, high = 0.0f, decayMs = 0.0f, periodic = 0.0f;
    bool silent = true;
};

int slotFor (HitKind kind);
HitFeatures analyseHit (const SampleData& data, int from, int to);
HitKind classifyHit (const HitFeatures& features);
HitKind classifyHit (const SampleData& data, int from, int to);

// Empty when the loop has no usable hits.
std::optional<BreakPlan> planBreak (const SampleData& data);

} // namespace batida
