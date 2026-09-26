#pragma once

#include "Pattern.h"

#include <string>
#include <vector>

namespace batida
{

// Guided variations of a pattern: the same idea as sound Vary, for steps.
// Each suggestion nudges the unlocked tracks one way (more hits, fewer, off
// the grid, ghost notes, rolls), guided by what each slot usually holds in
// the kit layout (1 kick, 3 snare, 7 and 8 hats, 6 bass). Tracks with no hits
// stay empty, the XY lane is kept, and near-duplicates never reach the user.

enum class PatternDirection { Any, Denser, Sparser, Broken, Ghosts, Rolls };

struct PatternVaryRequest
{
    Pattern base;
    float amount = 0.4f; // 0 subtle .. 1 far
    PatternDirection direction = PatternDirection::Any;
    std::array<bool, kNumTracks> locked {};
    uint32_t seed = 1;
    int count = 4;
};

struct PatternCandidate
{
    Pattern pattern;
    std::string note; // what changed, e.g. "+5 hits · 2 moved"
};

std::vector<PatternCandidate> varyPattern (const PatternVaryRequest& request);

// How many steps differ between two patterns (gate, velocity band, ratchet).
int patternDistance (const Pattern& a, const Pattern& b);

} // namespace batida
