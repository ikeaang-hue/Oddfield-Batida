#include "PatternVary.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace batida
{

namespace
{
// What a slot usually holds in the kit layout (SPEC §11).
enum class Role { Kick, Snare, Perc, Bass, Hat };

Role roleOf (int track)
{
    switch (track)
    {
        case 0: return Role::Kick;
        case 2: case 3: return Role::Snare;
        case 5: return Role::Bass;
        case 6: case 7: return Role::Hat;
        default: return Role::Perc;
    }
}

int lengthOf (const Pattern& p, int t)
{
    return std::clamp (std::min (p.tracks[(size_t) t].length, p.length), 1, kMaxSteps);
}

bool hasHits (const Pattern& p, int t)
{
    const auto len = lengthOf (p, t);
    for (int s = 0; s < len; ++s)
        if (p.tracks[(size_t) t].steps[(size_t) s].gate)
            return true;
    return false;
}

// The nearest hit to copy pitch and slice from, so added hits sound like the track.
const Step* nearestHit (const Track& tr, int len, int s)
{
    for (int d = 1; d < len; ++d)
        for (const auto n : { s - d, s + d })
            if (n >= 0 && n < len && tr.steps[(size_t) n].gate)
                return &tr.steps[(size_t) n];
    return nullptr;
}

struct Counts
{
    int added = 0, removed = 0, moved = 0, ghosts = 0, rolls = 0;
};

class Varier
{
public:
    Varier (Pattern& p, const PatternVaryRequest& r, juce::Random& rnd, Counts& c) : pat (p), req (r), random (rnd), counts (c) {}

    void apply (PatternDirection d, float a)
    {
        for (int t = 0; t < kNumTracks; ++t)
        {
            if (req.locked[(size_t) t] || ! hasHits (req.base, t))
                continue;
            switch (d)
            {
                case PatternDirection::Denser:  denser (t, a); break;
                case PatternDirection::Sparser: sparser (t, a); break;
                case PatternDirection::Broken:  broken (t, a); break;
                case PatternDirection::Ghosts:  ghosts (t, a); break;
                case PatternDirection::Rolls:   rolls (t, a); break;
                case PatternDirection::Any:     break;
            }
        }
    }

private:
    bool chance (float p) { return random.nextFloat() < p; }

    Step newHit (const Track& tr, int len, int s, int velLo, int velHi)
    {
        Step st;
        st.gate = true;
        st.velocity = (uint8_t) juce::jlimit (1, 127, velLo + random.nextInt (std::max (1, velHi - velLo)));
        if (const auto* near = nearestHit (tr, len, s))
        {
            st.pitch = near->pitch;
            st.slice = near->slice;
        }
        return st;
    }

    void denser (int t, float a)
    {
        const auto role = roleOf (t);
        if (role == Role::Bass)
            return;
        auto& tr = pat.tracks[(size_t) t];
        const auto len = lengthOf (pat, t);
        for (int s = 0; s < len; ++s)
        {
            if (tr.steps[(size_t) s].gate)
                continue;
            float w;
            switch (role)
            {
                case Role::Hat:   w = s % 2 == 0 ? 0.9f : 0.7f; break;
                case Role::Kick:  w = s % 4 == 0 ? 0.3f : (s % 2 == 0 ? 0.45f : 0.2f); break;
                case Role::Snare: w = s % 4 == 0 ? 0.08f : 0.18f; break;
                default:          w = s % 2 == 0 ? 0.3f : 0.35f; break;
            }
            if (chance (a * 0.4f * w))
            {
                const auto hat = role == Role::Hat;
                tr.steps[(size_t) s] = newHit (tr, len, s, hat ? 50 : 65, hat ? 95 : 110);
                ++counts.added;
            }
        }
    }

    void sparser (int t, float a)
    {
        auto& tr = pat.tracks[(size_t) t];
        const auto len = lengthOf (pat, t);
        int left = 0;
        for (int s = 0; s < len; ++s)
            left += tr.steps[(size_t) s].gate ? 1 : 0;
        for (int s = 0; s < len && left > 1; ++s)
        {
            auto& st = tr.steps[(size_t) s];
            if (! st.gate || (roleOf (t) == Role::Kick && s == 0))
                continue; // the kick on one stays
            const auto p = s % 4 == 0 ? a * 0.2f : a * 0.5f;
            if (chance (p))
            {
                st = {};
                --left;
                ++counts.removed;
            }
        }
    }

    void broken (int t, float a)
    {
        const auto role = roleOf (t);
        auto& tr = pat.tracks[(size_t) t];
        const auto len = lengthOf (pat, t);
        const auto p = role == Role::Hat ? a * 0.15f : a * 0.4f;
        std::array<bool, kMaxSteps> touched {};
        for (int s = 0; s < len; ++s)
        {
            if (! tr.steps[(size_t) s].gate || touched[(size_t) s] || (role == Role::Kick && s == 0) || ! chance (p))
                continue;
            const auto to = s + (random.nextBool() ? 1 : -1);
            if (to < 0 || to >= len || tr.steps[(size_t) to].gate)
                continue;
            tr.steps[(size_t) to] = tr.steps[(size_t) s];
            tr.steps[(size_t) s] = {};
            touched[(size_t) to] = true;
            ++counts.moved;
        }
    }

    void ghosts (int t, float a)
    {
        const auto role = roleOf (t);
        if (role == Role::Kick || role == Role::Bass)
            return;
        auto& tr = pat.tracks[(size_t) t];
        const auto len = lengthOf (pat, t);
        const auto p = role == Role::Snare ? a * 0.7f : a * 0.35f;
        std::array<bool, kMaxSteps> original {};
        for (int s = 0; s < len; ++s)
            original[(size_t) s] = tr.steps[(size_t) s].gate;
        for (int s = 0; s < len; ++s)
        {
            if (! original[(size_t) s] || ! chance (p))
                continue;
            static constexpr int offsets[] = { -1, 1, 2, -2, 3 };
            const auto to = s + offsets[random.nextInt (5)];
            if (to < 0 || to >= len || tr.steps[(size_t) to].gate)
                continue;
            tr.steps[(size_t) to] = newHit (tr, len, to, 18, 45);
            ++counts.ghosts;
        }
    }

    void rolls (int t, float a)
    {
        const auto role = roleOf (t);
        if (role == Role::Bass)
            return;
        auto& tr = pat.tracks[(size_t) t];
        const auto len = lengthOf (pat, t);
        const auto p = role == Role::Hat ? a * 0.45f : (role == Role::Kick ? a * 0.12f : a * 0.3f);
        const auto most = a > 0.6f ? 4 : 3;
        for (int s = 0; s < len; ++s)
        {
            auto& st = tr.steps[(size_t) s];
            if (! st.gate || st.ratchet > 1 || ! chance (p))
                continue;
            st.ratchet = (uint8_t) (2 + random.nextInt (most - 1));
            ++counts.rolls;
        }
        // A fill into the next bar: the last beat rolls on a snare or hat.
        if ((role == Role::Snare || role == Role::Hat) && len >= 8 && chance (a * 0.5f))
        {
            const auto s = len - 1 - random.nextInt (2);
            auto& st = tr.steps[(size_t) s];
            if (! st.gate)
            {
                st = newHit (tr, len, s, 70, 105);
                ++counts.added;
            }
            if (st.ratchet < 2)
            {
                st.ratchet = (uint8_t) most;
                ++counts.rolls;
            }
        }
    }

    Pattern& pat;
    const PatternVaryRequest& req;
    juce::Random& random;
    Counts& counts;
};

std::string noteFor (const Counts& c)
{
    std::vector<std::string> parts;
    auto add = [&] (int n, const char* what)
    {
        if (n > 0)
            parts.push_back (std::to_string (n) + what);
    };
    if (c.added > 0)
        parts.push_back ("+" + std::to_string (c.added) + (c.added == 1 ? " hit" : " hits"));
    if (c.removed > 0)
        parts.push_back ("\xe2\x88\x92" + std::to_string (c.removed) + (c.removed == 1 ? " hit" : " hits")); // −
    add (c.moved, " moved");
    add (c.ghosts, c.ghosts == 1 ? " ghost" : " ghosts");
    add (c.rolls, c.rolls == 1 ? " roll" : " rolls");
    std::string note;
    for (size_t i = 0; i < std::min<size_t> (3, parts.size()); ++i)
        note += (i > 0 ? " \xc2\xb7 " : "") + parts[i];
    return note;
}
} // namespace

int patternDistance (const Pattern& a, const Pattern& b)
{
    int d = 0;
    for (int t = 0; t < kNumTracks; ++t)
        for (int s = 0; s < kMaxSteps; ++s)
        {
            const auto& x = a.tracks[(size_t) t].steps[(size_t) s];
            const auto& y = b.tracks[(size_t) t].steps[(size_t) s];
            if (x.gate != y.gate)
                ++d;
            else if (x.gate && (x.ratchet != y.ratchet || std::abs ((int) x.velocity - (int) y.velocity) > 30))
                ++d;
        }
    return d;
}

std::vector<PatternCandidate> varyPattern (const PatternVaryRequest& req)
{
    const auto amount = std::clamp (req.amount, 0.05f, 1.0f);
    juce::Random random ((juce::int64) req.seed);
    const auto minDistance = std::max (2, (int) std::round (2.0f + 4.0f * amount));

    static constexpr PatternDirection mix[] = { PatternDirection::Denser, PatternDirection::Broken, PatternDirection::Ghosts,
                                                PatternDirection::Sparser, PatternDirection::Rolls };

    std::vector<PatternCandidate> chosen;
    for (int attempt = 0; attempt < 40 && (int) chosen.size() < req.count; ++attempt)
    {
        auto p = req.base;
        Counts counts;
        Varier varier (p, req, random, counts);
        if (req.direction == PatternDirection::Any)
        {
            // Each suggestion leans a different way, with a lighter second touch.
            varier.apply (mix[(size_t) ((int) chosen.size() + attempt) % 5], amount);
            varier.apply (mix[(size_t) random.nextInt (5)], amount * 0.4f);
        }
        else
            varier.apply (req.direction, amount);

        if (patternDistance (p, req.base) < minDistance)
            continue;
        const auto duplicate = std::any_of (chosen.begin(), chosen.end(), [&] (const PatternCandidate& c)
                                            { return patternDistance (p, c.pattern) < minDistance; });
        if (duplicate)
            continue;
        chosen.push_back ({ p, noteFor (counts) });
    }
    return chosen;
}

} // namespace batida
