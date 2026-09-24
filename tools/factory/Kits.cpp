#include "Factory.h"

#include <cstring>

// The factory kits (SPEC §11): the neutral kit, then 35 more across techno,
// breaks, glitch, house, garage, hip hop and trap. Each
// takes the reference layout (1 kick · 2 rim/perc · 3 snare · 4 clap ·
// 5 perc/tom · 6 bass · 7 closed hat · 8 open hat) unless its style needs a
// different sound in a slot, and then the replacement keeps the slot's job.
//
// Patterns are written as one string per track, one character per step:
//   X accent  x hit  o soft  g ghost  ? half the time  p a quarter of the time
//   2 3 4 ratchets  . rest

namespace batida::factory
{

namespace
{
struct P
{
    Pattern p;

    explicit P (int length)
    {
        p.length = length;
        for (auto& t : p.tracks)
            t.length = length;
    }

    P& row (int track, const char* steps)
    {
        auto& t = p.tracks[(size_t) track];
        if ((int) std::strlen (steps) > p.length)
        {
            std::fprintf (stderr, "a row is longer than its pattern: %s\n", steps);
            std::abort();
        }
        for (int i = 0; steps[i] != 0 && i < kMaxSteps; ++i)
        {
            auto& s = t.steps[(size_t) i];
            s = Step {};
            switch (steps[i])
            {
                case 'X': s.gate = true; s.velocity = 120; break;
                case 'x': s.gate = true; s.velocity = 100; break;
                case 'o': s.gate = true; s.velocity = 78; break;
                case 'g': s.gate = true; s.velocity = 42; break;
                case '?': s.gate = true; s.velocity = 80; s.probability = 50; break;
                case 'p': s.gate = true; s.velocity = 80; s.probability = 25; break;
                case '2': s.gate = true; s.velocity = 85; s.ratchet = 2; break;
                case '3': s.gate = true; s.velocity = 85; s.ratchet = 3; break;
                case '4': s.gate = true; s.velocity = 85; s.ratchet = 4; break;
                default: break;
            }
        }
        return *this;
    }

    // Pitches in semitones, one per step (the steps without a note are ignored).
    P& pitch (int track, std::initializer_list<int> semis)
    {
        int i = 0;
        for (auto v : semis)
            p.tracks[(size_t) track].steps[(size_t) i++].pitch = (int8_t) v;
        return *this;
    }

    P& slices (int track, std::initializer_list<int> which)
    {
        int i = 0;
        for (auto v : which)
            p.tracks[(size_t) track].steps[(size_t) i++].slice = (uint8_t) v;
        return *this;
    }

    P& length (int track, int steps) { p.tracks[(size_t) track].length = steps; return *this; }
    P& note (int track, float fraction) { p.tracks[(size_t) track].noteLength = fraction; return *this; }
    P& xy (int step, float x, float y) { p.xy[(size_t) step] = { true, x, y }; return *this; }

    operator Pattern() const { return p; }
};

// Kick 0, rim 1, snare 2, clap 3, perc 4, bass 5, closed hat 6, open hat 7.
enum { K, R, S, C, T, B, H, O };

void modulate (MovementData& m, std::array<float, kNumGlobalParams>& g, int mod, ShapePreset shape, int rate,
               bool global, int param, float depth, int voice = 0)
{
    auto& setup = m.mods[(size_t) mod];
    setPreset (setup, shape);
    for (auto& t : setup.targets)
        if (! t.active)
        {
            t = { true, global, voice, param, depth };
            break;
        }
    g[(size_t) modParam (mod, ModMode)] = 0.0f; // Sync
    g[(size_t) modParam (mod, ModRate)] = (float) rate;
    g[(size_t) modParam (mod, ModAmount)] = 1.0f;
}

// Mod Rate choices: 8 = 1/4, 10 = 1/2, 11 = 1 bar, 12 = 2 bars, 13 = 4 bars, 14 = 8 bars.
constexpr int k1Bar = 11, k2Bars = 12, k4Bars = 13, k8Bars = 14;

std::vector<KitConcept> build()
{
    std::vector<KitConcept> k;

    // 1. Neutral: the startup kit, as it is, with the breakbeat.
    {
        KitConcept c;
        c.name = "Neutral";
        c.style = "breaks";
        c.tags = { "warm", "organic" };
        c.neutral = true;
        c.pattern = [] { return breakbeatPattern(); };
        c.patternName = "Breakbeat";
        c.tempo = 120.0f;
        k.push_back (c);
    }

    // Techno -------------------------------------------------------------------------
    k.push_back ({ "Concrete", "techno", { "warm" },
        { { { "Kick", { "kick.boom", "kick.drop" }, { "warm" } },
            { "Rim", { "snare.rim" }, {}, -3.0f },
            { "Snare", { "snare.body" }, {}, -4.0f },
            { "Clap", { "snare.clap", "snare.clapstack" }, {}, -1.0f },
            { "Tom", { "perc.tom" }, {}, -3.0f, {}, 0.25f },
            { "Rumble", { "kick.sub" }, {}, -6.0f, { { "amp_decay", 900.0f }, { "flt_cutoff", 180.0f }, { "drive", 0.35f } } },
            { "Hat", { "hat.closed", "hat.metal" }, { "metallic" }, 0.0f, {}, -0.15f },
            { "Open Hat", { "hat.open" }, {}, -2.0f, {}, 0.15f } } },
        { { "xy_x", 0.35f }, { "xy_y", 0.35f }, { "comp_amount", 0.3f }, { "comp_attack", 8.0f }, { "comp_release", 150.0f },
          { "dist_drive", 0.1f }, { "eq_low", 1.5f }, { "eq_high", -1.0f } },
        [] (MovementData& m, auto& g) { modulate (m, g, 0, ShapePreset::Triangle, k4Bars, true, gp::XyY, 0.2f); },
        {},
        [] { return (Pattern) P (32)
                 .row (K, "X...X...X...X...X...X...X...X.o.")
                 .row (R, "......o.......o.......o....o..o.")
                 .row (C, "....x.......x.......x.......x...")
                 .row (T, "..o.....?.....o...o.....?.......")
                 .row (B, "..x...x...x...x...x...x...x...x.")
                 .row (H, "g.o.g.o.g.o.g.o.g.o.g.o.g.o.g.og")
                 .row (O, "..x...x...x...x...x...x...x...x.")
                 .xy (28, 0.45f, 0.6f).xy (29, 0.5f, 0.7f).xy (30, 0.55f, 0.8f).xy (31, 0.6f, 0.9f); },
        "Concrete", 130.0f, 0.5f });

    k.push_back ({ "Foundry", "techno", { "harsh", "metallic" },
        { { { "Kick", { "kick.dist" }, { "harsh" } },
            { "Metal", { "snare.metal", "hat.metal" }, { "metallic" }, -3.0f, {}, -0.2f },
            { "Snare", { "snare.crack", "snare.body" }, { "harsh" } },
            { "Clap", { "snare.clapstack", "snare.clap" }, {}, -1.0f },
            { "Tom", { "perc.tom" }, {}, -2.0f, { { "drive", 0.4f }, { "drive_type", 1 } }, 0.2f },
            { "Bass", { "bass.growl" }, { "harsh" }, -3.0f },
            { "Hat", { "hat.bit", "hat.metal" }, { "digital", "metallic" }, 0.0f, {}, -0.1f },
            { "Open Hat", { "hat.open" }, {}, -2.0f, {}, 0.1f } } },
        { { "xy_x", 0.7f }, { "xy_y", 0.55f }, { "comp_amount", 0.35f }, { "comp_attack", 5.0f }, { "dist_drive", 0.3f },
          { "exc_amount", 0.2f }, { "eq_low", 1.0f } },
        [] (MovementData& m, auto& g) { modulate (m, g, 0, ShapePreset::RampUp, k2Bars, true, gp::XyY, 0.25f); },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X..xX...X..xX.x.")
                 .row (R, "...o..o....o..x.")
                 .row (S, "....X.......X...")
                 .row (C, "....x.......x..g")
                 .row (T, "..........o..o..")
                 .row (B, "..o...o...o...oo")
                 .row (H, "xoxoxoxoxoxox2x4")
                 .row (O, "..x...x...x...x.")
                 .pitch (T, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -5 })
                 .xy (12, 0.8f, 0.8f).xy (14, 0.9f, 0.9f); },
        "Foundry", 138.0f, 0.5f });

    k.push_back ({ "Sparse", "techno", { "digital" },
        { { { "Kick", { "kick.tight", "kick.click" }, {} },
            { "Rim", { "snare.rim" }, {}, -2.0f, {}, 0.3f },
            { "Snap", { "snare.snap" }, {}, -3.0f },
            { "Clap", { "snare.clap" }, {}, -3.0f },
            { "Block", { "perc.block" }, {}, -3.0f, {}, -0.3f },
            { "Blip", { "perc.blip" }, { "digital" }, -4.0f, {}, 0.2f },
            { "Tick", { "hat.tick", "hat.closed" }, {}, 0.0f },
            { "Open Hat", { "hat.open" }, {}, -3.0f, { { "amp_decay", 250.0f } } } } },
        { { "xy_x", 0.5f }, { "xy_y", 0.15f }, { "comp_amount", 0.2f }, { "eq_hp", 30.0f } },
        [] (MovementData& m, auto& g) { modulate (m, g, 0, ShapePreset::Sine, k8Bars, true, gp::XyX, 0.2f); },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X...X...X...X...")
                 .row (R, "..o..?....o..?..")
                 .row (C, "....o.......o...")
                 .row (T, "......p.......p.")
                 .row (B, "..?.....o.....?.")
                 .row (H, ".og..og..og..og.")
                 .row (O, "..........x.....")
                 .length (T, 12).length (B, 7)
                 .pitch (B, { 0, 0, 7, 0, 0, 0, 0, 0, 12, 0, 0, 0, 0, 0, 3 }); },
        "Sparse", 126.0f, 0.5f });

    k.push_back ({ "Fathom", "techno", { "warm" },
        { { { "Kick", { "kick.boom", "kick.drop" }, { "warm" } },
            { "Rim", { "snare.rim" }, {}, -4.0f, { { "flt_cutoff", 400.0f } } },
            { "Snare", { "snare.body" }, {}, -4.0f, { { "flt_cutoff", 4000.0f } } },
            { "Clap", { "snare.clap", "snare.clapstack" }, {}, -2.0f, { { "flt_cutoff", 1000.0f } } },
            { "Chord", { "texture.chord" }, {}, -2.0f, { { "fm_pitch", -12.0f } } },
            { "Sub", { "bass.sub" }, {}, -3.0f },
            { "Hat", { "hat.closed" }, {}, -1.0f, { { "amp_decay", 60.0f } }, -0.2f },
            { "Open Hat", { "hat.open" }, {}, -3.0f, {}, 0.2f } } },
        { { "xy_x", 0.3f }, { "xy_y", 0.25f }, { "comp_amount", 0.25f }, { "comp_release", 250.0f }, { "eq_lp", 7000.0f },
          { "eq_low", 1.0f } },
        [] (MovementData& m, auto& g)
        {
            modulate (m, g, 0, ShapePreset::Triangle, k2Bars, true, gp::EqLp, -0.25f);
            modulate (m, g, 1, ShapePreset::Sine, k8Bars, false, vp::FltCutoff, 0.3f, 4);
        },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X...X...X...X...")
                 .row (R, "..........o.....")
                 .row (C, "....o.......o...")
                 .row (T, "..x..o....x..o..")
                 .row (B, "..o...o...o...o.")
                 .row (H, "g.o.g.o.g.o.g.o.")
                 .row (O, "......x.......x.")
                 .row (S, "...............p")
                 .pitch (T, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -2, 0, 0, 0 })
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -2, 0, 0, 0, 0 }); },
        "Fathom", 120.0f, 0.5f });

    k.push_back ({ "Solvent", "techno", { "harsh" },
        { { { "Kick", { "kick.drop", "kick.tight" }, {} },
            { "Rim", { "snare.rim" }, {}, -3.0f },
            { "Snare", { "snare.body" }, {}, -4.0f },
            { "Clap", { "snare.clap", "snare.clapstack" }, {}, -1.0f },
            { "Acid 2", { "bass.acid" }, {}, -5.0f, { { "fm_pitch", 0.0f } }, 0.3f },
            { "Acid", { "bass.acid" }, {}, -2.0f },
            { "Hat", { "hat.closed" }, {}, 0.0f, {}, -0.15f },
            { "Open Hat", { "hat.open" }, {}, -2.0f, {}, 0.15f } } },
        { { "xy_x", 0.55f }, { "xy_y", 0.45f }, { "comp_amount", 0.4f }, { "dist_drive", 0.15f } },
        [] (MovementData& m, auto& g)
        {
            modulate (m, g, 0, ShapePreset::Triangle, k4Bars, false, vp::FltCutoff, 0.3f, 5);
            modulate (m, g, 1, ShapePreset::RandomSteps, k1Bar, false, vp::FltRes, 0.1f, 5);
        },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X...X...X...X...")
                 .row (R, "...........o....")
                 .row (C, "....x.......x...")
                 .row (T, "x.......?.......")
                 .row (B, "xoxxoxoxxoxoxxox")
                 .row (H, "gogxgogxgogxgogx")
                 .row (O, "..x...x...x...x.")
                 .pitch (T, { 12, 0, 0, 0, 0, 0, 0, 0, 7 })
                 .pitch (B, { 0, 0, 12, 0, 3, 0, -2, 0, 0, 12, 0, 5, 0, 3, 0, 7 })
                 .note (B, 0.7f); },
        "Solvent", 132.0f, 0.5f });

    // Breaks --------------------------------------------------------------------------
    k.push_back ({ "Reel", "breaks", { "warm", "organic" },
        { { { "Kick", { "kick.dusty", "kick.drop" }, { "warm" } },
            { "Rim", { "snare.rim" }, {}, -3.0f },
            { "Snare", { "snare.fat", "snare.dusty" }, { "warm" } },
            { "Clap", { "snare.clap" }, {}, -3.0f },
            { "Tom", { "perc.tom" }, {}, -2.0f },
            { "Bass", { "bass.sub", "bass.pluck" }, { "warm" }, -2.0f },
            { "Hat", { "hat.closed" }, {}, 0.0f, {}, -0.2f },
            { "Open Hat", { "hat.open" }, {}, -2.0f, {}, 0.2f } } },
        { { "xy_x", 0.2f }, { "xy_y", 0.35f }, { "comp_amount", 0.4f }, { "comp_mix", 0.7f }, { "dist_type", 0.35f } },
        {}, {},
        [] { return (Pattern) P (32)
                 .row (K, "X.x.......x..x..X.x...x...x..x..")
                 .row (S, "....X..g.g..X..g....X..g.g..X.g.")
                 .row (H, "x.x.x.x.x.x.x.?.x.x.x.x.x.x.x...")
                 .row (O, ".............x..............x...")
                 .row (R, "..........?...................?.")
                 .row (B, "X.....o...o..o..X.....o...o.....")
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 5, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, -2, 0, 0, 0, -5 }); },
        "Reel", 130.0f, 0.5f });

    k.push_back ({ "Tension", "breaks", { "harsh" },
        { { { "Kick", { "kick.tight", "kick.click" }, {} },
            { "Rim", { "snare.rim" }, {}, -3.0f },
            { "Snare", { "snare.crack" }, { "harsh" } },
            { "Clap", { "snare.clapstack" }, {}, -2.0f },
            { "Perc", { "perc.conga", "perc.tom" }, {}, -3.0f, {}, 0.3f },
            { "Bass", { "bass.growl", "bass.808dist" }, {}, -2.0f },
            { "Hat", { "hat.closed" }, {}, 0.0f, {}, -0.15f },
            { "Open Hat", { "hat.open" }, {}, -2.0f, {}, 0.15f } } },
        { { "xy_x", 0.55f }, { "xy_y", 0.4f }, { "comp_amount", 0.3f }, { "comp_attack", 4.0f } },
        [] (MovementData& m, auto& g) { modulate (m, g, 0, ShapePreset::RampUp, k4Bars, true, gp::XyY, 0.2f); },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X......x..x.....")
                 .row (S, "....X.......X...")
                 .row (R, "..g....g.g....g.")
                 .row (C, "....o.......o...")
                 .row (H, "xoxoxoxoxoxoxoxo")
                 .row (O, "......x.......x.")
                 .row (B, "x.....x.x.....x.")
                 .row (T, "...o......o.....")
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 3, 0, -2, 0, 0, 0, 0, 0, 5 }); },
        "Tension", 132.0f, 0.5f });

    k.push_back ({ "Vector", "breaks", { "digital" },
        { { { "Kick", { "kick.boom", "kick.drop" }, {}, 0.0f, { { "amp_decay", 700.0f } } },
            { "Zap", { "perc.zap" }, { "digital" }, -4.0f, {}, -0.25f },
            { "Snare", { "snare.body", "snare.snap" }, {} },
            { "Clap", { "snare.clap" }, {}, -2.0f },
            { "Tom", { "perc.tom" }, {}, -2.0f, {}, 0.2f },
            { "808", { "bass.808" }, {}, -2.0f },
            { "Hat", { "hat.closed", "hat.tick" }, {}, 0.0f },
            { "Open Hat", { "hat.open" }, {}, -2.0f } } },
        { { "xy_x", 0.6f }, { "xy_y", 0.3f }, { "comp_amount", 0.35f } },
        {}, {},
        [] { return (Pattern) P (16)
                 .row (K, "X.....x...X.....")
                 .row (R, "..o.....?.....o.")
                 .row (S, "....X.......X...")
                 .row (C, ".......o........")
                 .row (T, "...........o.oo.")
                 .row (B, "X.....x...X..x..")
                 .row (H, "o.o.o.x.o.o.o.x.")
                 .row (O, "..............x.")
                 .pitch (T, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, -3 })
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, -2 }); },
        "Vector", 125.0f, 0.5f });

    k.push_back ({ "Rapid", "breaks", { "organic" },
        { { { "Kick", { "kick.tight", "kick.drop" }, {} },
            { "Rim", { "snare.rim" }, {}, -3.0f },
            { "Snare", { "snare.crack", "snare.body" }, {} },
            { "Snare 2", { "snare.body" }, {}, -4.0f, { { "fm_pitch", -2.0f } } },
            { "Conga", { "perc.conga" }, {}, -4.0f, {}, 0.3f },
            { "Sub", { "bass.sub" }, {}, -1.0f },
            { "Hat", { "hat.closed" }, {}, -1.0f, {}, -0.2f },
            { "Ride", { "hat.ride" }, {}, -4.0f, {}, 0.25f } } },
        { { "xy_x", 0.4f }, { "xy_y", 0.3f }, { "comp_amount", 0.5f }, { "comp_attack", 3.0f }, { "comp_release", 80.0f } },
        {}, {},
        [] { return (Pattern) P (32)
                 .row (K, "X.........x.....X.x.......x.....")
                 .row (S, "....X.....g.X.......X..g..X...g.")
                 .row (C, "......g.......g.......g....2....")
                 .row (H, "o.o.o.o.o.o.o.o.o.o.o.o.o.o.o.o.")
                 .row (O, "x...............x...............")
                 .row (B, "X...............x.......o.......")
                 .row (T, "...........p...............p....")
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -5, 0, 0, 0, 0, 0, 0, 0, -2 }); },
        "Rapid", 170.0f, 0.5f });

    // Glitch --------------------------------------------------------------------------
    k.push_back ({ "Bitrate", "glitch", { "digital" },
        { { { "Kick", { "kick.click" }, { "digital" } },
            { "Blip", { "perc.blip" }, {}, -4.0f, {}, -0.3f },
            { "Snare", { "snare.snap", "snare.metal" }, {} },
            { "Clap", { "snare.clapstack" }, {}, -2.0f, { { "drive", 0.5f }, { "drive_type", 3 } } },
            { "Zap", { "perc.zap" }, {}, -3.0f, {}, 0.3f },
            { "Bass", { "bass.growl", "bass.pluck" }, {}, -3.0f },
            { "Hat", { "hat.bit" }, { "digital" }, 0.0f },
            { "Crackle", { "texture.crackle" }, {}, -4.0f } } },
        { { "xy_x", 0.9f }, { "xy_y", 0.5f }, { "comp_amount", 0.4f }, { "exc_amount", 0.15f } },
        [] (MovementData& m, auto& g)
        {
            modulate (m, g, 0, ShapePreset::RandomSteps, k1Bar, true, gp::XyX, 0.3f);
            modulate (m, g, 1, ShapePreset::RandomSteps, k2Bars, true, gp::XyY, 0.25f);
        },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X..x..X...x.X...")
                 .row (R, "?.?..?..?.?..?.?")
                 .row (S, "....X..2....X.4.")
                 .row (C, "......?.........")
                 .row (T, "..3.....?..2....")
                 .row (B, "X.....x..x....x.")
                 .row (H, "4.o.2.o.3.o.o.2.")
                 .row (O, "x...............")
                 .pitch (R, { 0, 0, 7, 0, 0, 12, 0, 0, 5, 0, 3, 0, 0, 10, 0, -2 })
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 7, 0, 0, 3, 0, 0, 0, 0, -2 })
                 .xy (7, 1.0f, 0.8f).xy (14, 0.95f, 1.0f); },
        "Bitrate", 120.0f, 0.5f });

    k.push_back ({ "Shards", "glitch", { "digital" },
        { { { "Kick", { "kick.tight" }, {} },
            { "Rim", { "snare.rim" }, {}, -3.0f },
            { "Snap", { "snare.snap" }, {}, -2.0f },
            { "Clap", { "snare.clap" }, {}, -3.0f },
            { "Loop", { "texture.loop" }, {}, 0.0f },
            { "Bass", { "bass.pluck" }, {}, -3.0f },
            { "Tick", { "hat.tick" }, {}, 0.0f },
            { "Glitch", { "fx.glitch" }, {}, -4.0f } } },
        { { "xy_x", 0.75f }, { "xy_y", 0.3f }, { "comp_amount", 0.35f } },
        [] (MovementData& m, auto& g) { modulate (m, g, 0, ShapePreset::RandomSteps, k1Bar, true, gp::XyY, 0.3f); },
        {},
        [] { return (Pattern) P (32)
                 .row (T, "xxxxxxxxxxxxxxxxxxxxxxx2xxxxx4xx")
                 .slices (T, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                               0, 1, 2, 3, 0, 1, 6, 7, 8, 8, 10, 11, 4, 13, 14, 14 })
                 .row (K, "X.......X..x....X.......X.....x.")
                 .row (S, "....x.......x.......x.......x...")
                 .row (H, ".?.?.?.?.?.?.?.?.?.?.?.?.?.?.?.?")
                 .row (B, "x......x........x......x..x.....")
                 .row (O, "...........................p....")
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -2, 0, 0, 5 }); },
        "Shards", 110.0f, 0.5f });

    k.push_back ({ "Alloy", "glitch", { "metallic" },
        { { { "Kick", { "kick.tight", "kick.click" }, {} },
            { "Metal", { "snare.metal" }, { "metallic" }, -4.0f, {}, -0.3f },
            { "Snare", { "snare.metal", "snare.body" }, { "metallic" } },
            { "Clap", { "snare.clap" }, {}, -3.0f },
            { "Bell", { "perc.cowbell" }, {}, -4.0f, {}, 0.3f },
            { "Bass", { "bass.pluck" }, {}, -3.0f, { { "fm_harm", 0.5f } } },
            { "Hat", { "hat.metal" }, { "metallic" }, 0.0f, {}, -0.15f },
            { "Ride", { "hat.ride" }, {}, -3.0f, {}, 0.2f } } },
        { { "xy_x", 0.65f }, { "xy_y", 0.35f }, { "comp_amount", 0.35f }, { "exc_amount", 0.25f } },
        [] (MovementData& m, auto& g) { modulate (m, g, 0, ShapePreset::Sine, k4Bars, true, gp::XyX, 0.25f); },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X...X...X..xX...")
                 .row (R, "..o..o..o...o.o.")
                 .row (S, "....x.......x...")
                 .row (T, "...o..?....o..?.")
                 .row (B, "..o...o...o...o.")
                 .row (H, ".o.o.o.o.o.o.o.o")
                 .row (O, "x...x...x...x...")
                 .pitch (T, { 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 7 })
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 7, 0, 0, 0, 5 }); },
        "Alloy", 125.0f, 0.5f });

    // House ----------------------------------------------------------------------------
    k.push_back ({ "Floor", "house", { "warm" },
        { { { "Kick", { "kick.house" }, {} },
            { "Shaker", { "hat.shaker" }, {}, -3.0f, {}, 0.3f },
            { "Snare", { "snare.body" }, {}, -4.0f },
            { "Clap", { "snare.clapstack" }, {}, 0.0f },
            { "Conga", { "perc.conga" }, {}, -3.0f, {}, -0.3f },
            { "Bass", { "bass.sub", "bass.pluck" }, {}, -2.0f },
            { "Hat", { "hat.closed" }, {}, -1.0f, {}, -0.1f },
            { "Open Hat", { "hat.open" }, {}, -1.0f, {}, 0.1f } } },
        { { "xy_x", 0.35f }, { "xy_y", 0.3f }, { "comp_amount", 0.3f }, { "comp_mix", 0.7f } },
        {}, {},
        [] { return (Pattern) P (16)
                 .row (K, "X...X...X...X...")
                 .row (R, "gogogogogogogogo")
                 .row (S, "............g...")
                 .row (C, "....X.......X...")
                 .row (T, "...o.o.....o..o.")
                 .row (B, "..x...x...x..ox.")
                 .row (H, "g.o.g.o.g.o.g.o.")
                 .row (O, "..x...x...x...x.")
                 .pitch (T, { 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 7 })
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 12 }); },
        "Floor", 124.0f, 0.52f });

    k.push_back ({ "Velvet", "house", { "warm" },
        { { { "Kick", { "kick.house", "kick.drop" }, { "warm" } },
            { "Rim", { "snare.rim" }, {}, -4.0f, {}, 0.2f },
            { "Snare", { "snare.body" }, {}, -5.0f, { { "flt_cutoff", 5000.0f } } },
            { "Clap", { "snare.clap", "snare.clapstack" }, {}, -1.0f },
            { "Chord", { "texture.chord" }, {}, -2.0f, { { "fm_pitch", -12.0f } } },
            { "Bass", { "bass.sub", "bass.pluck" }, { "warm" }, -2.0f },
            { "Shaker", { "hat.shaker", "hat.closed" }, {}, -2.0f, {}, -0.2f },
            { "Open Hat", { "hat.open" }, {}, -3.0f, {}, 0.2f } } },
        { { "xy_x", 0.2f }, { "xy_y", 0.2f }, { "comp_amount", 0.3f }, { "eq_lp", 12000.0f }, { "eq_high", -2.0f } },
        [] (MovementData& m, auto& g) { modulate (m, g, 0, ShapePreset::Sine, k8Bars, false, vp::FltCutoff, 0.25f, 4); },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X...X...X...X...")
                 .row (R, "......o.......o.")
                 .row (C, "....x.......x...")
                 .row (T, "..x.....?.x.....")
                 .row (B, "x..o..x...o..x..")
                 .row (H, "..o...o...o...o.")
                 .row (O, "......x.......x.")
                 .pitch (T, { 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, -2 })
                 .pitch (B, { 0, 0, 0, 12, 0, 0, 3, 0, 0, 0, 5, 0, 0, 7 }); },
        "Velvet", 120.0f, 0.56f });

    k.push_back ({ "Rolling", "house", { "organic" },
        { { { "Kick", { "kick.house", "kick.tight" }, {} },
            { "Rim", { "snare.rim" }, {}, -3.0f, {}, 0.25f },
            { "Snap", { "snare.snap" }, {}, -4.0f },
            { "Clap", { "snare.clapstack" }, {}, 0.0f },
            { "Perc", { "perc.conga", "perc.tom" }, {}, -3.0f, {}, -0.25f },
            { "Bass", { "bass.pluck", "bass.growl" }, {}, -2.0f },
            { "Hat", { "hat.closed" }, {}, -1.0f },
            { "Open Hat", { "hat.open" }, {}, -2.0f } } },
        { { "xy_x", 0.45f }, { "xy_y", 0.35f }, { "comp_amount", 0.3f } },
        [] (MovementData& m, auto& g) { modulate (m, g, 0, ShapePreset::Triangle, k4Bars, true, gp::XyY, 0.15f); },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X...X...X...X...")
                 .row (R, "..o..o....o..o.o")
                 .row (S, "............o...")
                 .row (C, "....X.......X...")
                 .row (T, "...o..o...o.o...")
                 .row (B, "..xo..xo..xo..xo")
                 .row (H, "oxoxoxoxoxoxoxox")
                 .row (O, "..x...x...x...x.")
                 .pitch (T, { 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 5 })
                 .pitch (B, { 0, 0, 0, 12, 0, 0, 0, 12, 0, 0, 3, 15, 0, 0, -2, 10 }); },
        "Rolling", 126.0f, 0.54f });

    // Garage ---------------------------------------------------------------------------
    k.push_back ({ "Shuffle", "garage", { "organic" },
        { { { "Kick", { "kick.house", "kick.tight" }, {} },
            { "Rim", { "snare.rim" }, {}, -2.0f, {}, 0.2f },
            { "Snare", { "snare.body", "snare.crack" }, {} },
            { "Clap", { "snare.clap" }, {}, -3.0f },
            { "Stab", { "bass.stab" }, {}, -3.0f, { { "fm_pitch", -12.0f } } },
            { "Bass", { "bass.sub", "bass.growl" }, {}, -2.0f },
            { "Hat", { "hat.closed", "hat.shaker" }, {}, -1.0f, {}, -0.2f },
            { "Open Hat", { "hat.open" }, {}, -2.0f, {}, 0.2f } } },
        { { "xy_x", 0.4f }, { "xy_y", 0.3f }, { "comp_amount", 0.4f } },
        {}, {},
        [] { return (Pattern) P (32)
                 .row (K, "X.........X.....X......x..X.....")
                 .row (R, "....x..o....x.......x..o....x...")
                 .row (S, "....X.......X.......X.......X...")
                 .row (C, "....o.......o.......o.......o...")
                 .row (T, "..x..x.....x..........x..x......")
                 .row (B, "x.....x...x..x..x.....x...x.....")
                 .row (H, "..o.?.o...o.?.o...o.?.o...o.?.o.")
                 .row (O, "..............x...............x.")
                 .pitch (T, { 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, -2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 3 })
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, -2, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0 }); },
        "Shuffle", 132.0f, 0.62f });

    k.push_back ({ "Weight", "garage", { "harsh" },
        { { { "Kick", { "kick.tight", "kick.house" }, {} },
            { "Rim", { "snare.rim" }, {}, -3.0f },
            { "Snare", { "snare.crack", "snare.body" }, {} },
            { "Clap", { "snare.clapstack" }, {}, -2.0f },
            { "Tom", { "perc.tom" }, {}, -3.0f },
            { "Bass", { "bass.growl", "bass.808dist" }, { "harsh" }, 0.0f, { { "glide", 60.0f } } },
            { "Hat", { "hat.tick", "hat.closed" }, {}, -1.0f },
            { "Open Hat", { "hat.open" }, {}, -2.0f } } },
        { { "xy_x", 0.55f }, { "xy_y", 0.45f }, { "comp_amount", 0.45f }, { "dist_low_keep", 140.0f } },
        [] (MovementData& m, auto& g) { modulate (m, g, 0, ShapePreset::Triangle, k2Bars, false, vp::FltCutoff, 0.3f, 5); },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X.....x...X.....")
                 .row (R, "..o........o....")
                 .row (S, "....X.......X...")
                 .row (C, "....o.......o...")
                 .row (B, "x..x..x...x.x...")
                 .row (H, "o.oo.oo.o.oo.oo.")
                 .row (O, "......x.......x.")
                 .row (T, "..............p.")
                 .pitch (B, { 0, 0, 0, 0, 0, 0, -2, 0, 0, 0, 3, 0, 5 })
                 .note (B, 0.9f); },
        "Weight", 134.0f, 0.58f });

    // Hip hop --------------------------------------------------------------------------
    k.push_back ({ "Sepia", "hip hop", { "warm", "organic" },
        { { { "Kick", { "kick.dusty" }, { "warm" } },
            { "Rim", { "snare.rim" }, {}, -3.0f },
            { "Snare", { "snare.dusty", "snare.fat" }, { "warm" } },
            { "Rim Snare", { "snare.rim" }, {}, -1.0f, { { "flt_cutoff", 200.0f } } },
            { "Perc", { "perc.conga", "perc.tamb" }, {}, -4.0f, {}, 0.3f },
            { "Bass", { "bass.pluck", "bass.sub" }, { "warm" }, -2.0f },
            { "Hat", { "hat.closed" }, { "organic" }, -1.0f, { { "flt_cutoff", 7000.0f } }, -0.15f },
            { "Open Hat", { "hat.open" }, {}, -3.0f, {}, 0.15f } } },
        { { "xy_x", 0.15f }, { "xy_y", 0.4f }, { "comp_amount", 0.45f }, { "comp_mix", 0.8f }, { "eq_lp", 9000.0f },
          { "eq_low", 1.5f } },
        {}, {},
        [] { return (Pattern) P (16)
                 .row (K, "X......xX.x.....")
                 .row (S, "....X.......X...")
                 .row (C, "..........g....g")
                 .row (T, "......?.........")
                 .row (B, "X......oX.x.....")
                 .row (H, "x.o.x.o.x.o.x.o.")
                 .row (O, "..............o.")
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 0, -2, 0, 0, 3 }); },
        "Sepia", 90.0f, 0.6f });

    k.push_back ({ "Faded", "hip hop", { "warm" },
        { { { "Kick", { "kick.dusty", "kick.drop" }, { "warm" } },
            { "Rim", { "snare.rim" }, {}, -4.0f },
            { "Snare", { "snare.dusty" }, {} },
            { "Clap", { "snare.clap" }, {}, -3.0f, { { "flt_cutoff", 900.0f } } },
            { "Block", { "perc.block", "perc.conga" }, {}, -5.0f, {}, 0.3f },
            { "Bass", { "bass.sub", "bass.pluck" }, { "warm" }, -2.0f },
            { "Shaker", { "hat.shaker", "hat.closed" }, {}, -2.0f, {}, -0.2f },
            { "Crackle", { "texture.crackle" }, {}, -6.0f } } },
        { { "xy_x", 0.1f }, { "xy_y", 0.3f }, { "comp_amount", 0.35f }, { "eq_lp", 6000.0f }, { "eq_high", -3.0f },
          { "dist_type", 0.1f } },
        [] (MovementData& m, auto& g) { modulate (m, g, 0, ShapePreset::Sine, k2Bars, true, gp::EqLp, -0.08f); },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X.....x...x.....")
                 .row (S, "....X.......X...")
                 .row (R, ".......g.....g..")
                 .row (T, "......o.........")
                 .row (B, "X.....o...o.....")
                 .row (H, "o.g.o.g.o.g.o.g.")
                 .row (O, "x...............")
                 .pitch (B, { 0, 0, 0, 0, 0, 0, -2, 0, 0, 0, -5 }); },
        "Faded", 82.0f, 0.62f });

    // Trap ------------------------------------------------------------------------------
    k.push_back ({ "Slide", "trap", {},
        { { { "Kick", { "kick.tight", "kick.click" }, {} },
            { "Rim", { "snare.rim" }, {}, -4.0f },
            { "Snare", { "snare.body", "snare.clapstack" }, {} },
            { "Snap", { "snare.snap" }, {}, 0.0f },
            { "Perc", { "perc.conga", "perc.tom" }, {}, -4.0f, {}, 0.3f },
            { "808", { "bass.808" }, {}, 0.0f },
            { "Hat", { "hat.tick", "hat.closed" }, {}, -1.0f, {}, -0.1f },
            { "Open Hat", { "hat.open" }, {}, -3.0f, {}, 0.1f } } },
        { { "xy_x", 0.5f }, { "xy_y", 0.3f }, { "comp_amount", 0.35f } },
        {}, {},
        [] { return (Pattern) P (32)
                 .row (K, "X......x..X.....X.x.......X..x..")
                 .row (C, "........x...............x.......")
                 .row (S, "........o...............o.......")
                 .row (H, "x.x.x.x.x.x.x.3.x.x.x.x.x.x.4.2.")
                 .row (O, "......x.........................")
                 .row (B, "X......x..X.....X.x.......X..x..")
                 .row (T, "...o.........o.....o............")
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -5, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, -2, 0, 0, -7 })
                 .note (B, 1.0f); },
        "Slide", 140.0f, 0.5f });

    k.push_back ({ "Blackout", "trap", { "harsh" },
        { { { "Kick", { "kick.tight" }, {} },
            { "Rim", { "snare.rim" }, {}, -3.0f },
            { "Snare", { "snare.crack" }, { "harsh" } },
            { "Snap", { "snare.snap" }, {}, -1.0f },
            { "Perc", { "perc.zap", "perc.block" }, {}, -5.0f, {}, 0.3f },
            { "808", { "bass.808dist", "bass.808" }, { "harsh" }, 0.0f, { { "glide", 90.0f } } },
            { "Hat", { "hat.tick", "hat.metal" }, {}, -1.0f },
            { "Open Hat", { "hat.open" }, {}, -3.0f } } },
        { { "xy_x", 0.65f }, { "xy_y", 0.4f }, { "comp_amount", 0.4f }, { "eq_lp", 11000.0f } },
        {}, {},
        [] { return (Pattern) P (32)
                 .row (K, "X.........X.....X.....x...X.....")
                 .row (C, "........x...............x....x..")
                 .row (S, "........o...............o.......")
                 .row (H, "x..x..x.x..x..x.x..3..x.x..x..4.")
                 .row (B, "X.....x...x..x..X.....x...x..x..")
                 .row (T, "......p...............p.........")
                 .row (R, "...........o...............o....")
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, -2, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 3, 0, 0, -4 })
                 .note (B, 1.0f); },
        "Blackout", 142.0f, 0.5f });

    // More kits (after the first listen): built around the aggressive sounds,
    // plus a few moods the first set didn't cover. Each has a B pattern too
    // (extraPatterns): a build, a break or a fill.

    // Techno ----------------------------------------------------------------------------
    k.push_back ({ "Pressure", "techno", { "harsh" },
        { { { "Kick", { "kick.hard" }, { "harsh" } },
            { "Rim", { "snare.rimhard" }, {}, -4.0f, {}, 0.25f },
            { "Snare", { "snare.slam" }, {}, -4.0f },
            { "Clap", { "snare.clapdist" }, {}, -1.0f },
            { "Tom", { "perc.tomhard" }, {}, -3.0f, {}, -0.25f },
            { "Rumble", { "kick.sub" }, {}, -6.0f, { { "amp_decay", 900.0f }, { "flt_cutoff", 180.0f }, { "drive", 0.5f } } },
            { "Hat", { "hat.hard" }, {}, 0.0f, {}, -0.1f },
            { "Open Hat", { "hat.openhard" }, {}, -2.0f, {}, 0.1f } } },
        { { "xy_x", 0.6f }, { "xy_y", 0.5f }, { "comp_amount", 0.2f }, { "comp_attack", 4.0f }, { "dist_drive", 0.25f },
          { "exc_amount", 0.15f }, { "eq_low", 1.0f } },
        [] (MovementData& m, auto& g) { modulate (m, g, 0, ShapePreset::RampUp, k4Bars, true, gp::XyY, 0.25f); },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X...X...X...X...")
                 .row (R, "...o......o...o.")
                 .row (C, "....x.......x...")
                 .row (S, "............g..g")
                 .row (T, "......o......o..")
                 .row (B, "..x...x...x...x.")
                 .row (H, "xoxoxoxoxoxoxoxo")
                 .row (O, "..x...x...x...x.")
                 .pitch (T, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -3 }); },
        "Pressure", 140.0f, 0.5f });

    k.push_back ({ "Furnace", "techno", { "harsh", "digital" },
        { { { "Kick", { "kick.gabber" }, { "harsh" } },
            { "Rim", { "snare.rimhard" }, {}, -4.0f },
            { "Snare", { "snare.slam" }, {}, -2.0f },
            { "Clap", { "snare.clapdist" }, {}, -2.0f },
            { "Tom", { "perc.tomhard" }, {}, -3.0f, {}, 0.25f },
            { "Screech", { "fx.screech" }, {}, -5.0f, {}, -0.2f },
            { "Hat", { "hat.hard" }, {}, -1.0f },
            { "Open Hat", { "hat.openhard" }, {}, -2.0f } } },
        { { "xy_x", 0.75f }, { "xy_y", 0.6f }, { "comp_amount", 0.2f }, { "dist_drive", 0.4f } },
        [] (MovementData& m, auto& g) { modulate (m, g, 0, ShapePreset::Square, k2Bars, true, gp::XyX, 0.2f); },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X...X...X...X...")
                 .row (C, "....x.......x...")
                 .row (H, "..x...x...x...x.")
                 .row (B, "x.......x.......")
                 .row (T, "..........o.o.o.")
                 .row (R, "...o.......o....")
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 0, 0, 5 })
                 .pitch (T, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -3, 0, -5 }); },
        "Furnace", 175.0f, 0.5f });

    k.push_back ({ "Tunnel", "techno", { "metallic" },
        { { { "Kick", { "kick.drop", "kick.boom" }, {} },
            { "Rim", { "snare.rim" }, {}, -3.0f, {}, 0.3f },
            { "Snare", { "snare.body" }, {}, -5.0f },
            { "Clap", { "snare.clap" }, {}, -3.0f },
            { "Clang", { "perc.clang" }, {}, -5.0f, {}, -0.3f },
            { "Rumble", { "kick.sub" }, {}, -6.0f, { { "amp_decay", 900.0f }, { "flt_cutoff", 160.0f }, { "drive", 0.3f } } },
            { "Hat", { "hat.metal", "hat.closed" }, {}, -1.0f, {}, -0.15f },
            { "Ride", { "hat.ride" }, {}, -4.0f, {}, 0.2f } } },
        { { "xy_x", 0.45f }, { "xy_y", 0.3f }, { "comp_amount", 0.2f }, { "comp_release", 200.0f } },
        [] (MovementData& m, auto& g)
        {
            modulate (m, g, 0, ShapePreset::Sine, k8Bars, true, gp::XyX, 0.3f);
            modulate (m, g, 1, ShapePreset::Triangle, k2Bars, false, vp::FltCutoff, 0.3f, 4);
        },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X...X...X...X...")
                 .row (B, "..x...x...x...x.")
                 .row (T, "x..o..x")
                 .row (R, "..o....?....")
                 .row (H, "g.o.g.o.g.o.g.o.")
                 .row (O, "x...x...x...x...")
                 .row (C, "....o.......o...")
                 .length (T, 7).length (R, 12)
                 .pitch (T, { 0, 0, 0, 5, 0, 0, -2 }); },
        "Tunnel", 128.0f, 0.5f });

    // Breaks -----------------------------------------------------------------------------
    k.push_back ({ "Splinter", "breaks", { "harsh" },
        { { { "Kick", { "kick.punch" }, {} },
            { "Rim", { "snare.rimhard" }, {}, -4.0f, {}, 0.2f },
            { "Snare", { "snare.slam" }, {} },
            { "Snare 2", { "snare.fold" }, {}, -3.0f },
            { "Zap", { "perc.zaphard" }, {}, -5.0f, {}, -0.3f },
            { "Reese", { "bass.reese" }, {}, -2.0f },
            { "Hat", { "hat.hard" }, {}, -1.0f, {}, -0.15f },
            { "Crash", { "hat.crash" }, {}, -5.0f, {}, 0.2f } } },
        { { "xy_x", 0.65f }, { "xy_y", 0.55f }, { "comp_amount", 0.2f }, { "comp_attack", 3.0f } },
        [] (MovementData& m, auto& g) { modulate (m, g, 0, ShapePreset::RandomSteps, k1Bar, true, gp::XyY, 0.2f); },
        {},
        [] { return (Pattern) P (32)
                 .row (K, "X.x.......x.....X.x...x...x.....")
                 .row (S, "....X..g.g..X..g....X..g..X.X...")
                 .row (C, "......2.......g.......4.......3.")
                 .row (H, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.")
                 .row (O, "x...............................")
                 .row (B, "X...........x...X.......x.......")
                 .row (T, "...............p...........3....")
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -2 })
                 .note (B, 0.9f); },
        "Splinter", 172.0f, 0.5f });

    k.push_back ({ "Voltage", "breaks", { "digital" },
        { { { "Kick", { "kick.punch", "kick.boom" }, {} },
            { "Zap", { "perc.zaphard", "perc.zap" }, {}, -5.0f, {}, -0.25f },
            { "Snare", { "snare.slam", "snare.snap" }, {} },
            { "Clap", { "snare.clapdist" }, {}, -2.0f },
            { "Tom", { "perc.tomhard" }, {}, -3.0f, {}, 0.2f },
            { "808", { "bass.808clip" }, {}, -1.0f },
            { "Hat", { "hat.metalhard", "hat.tick" }, {}, -1.0f },
            { "Open Hat", { "hat.open" }, {}, -3.0f } } },
        { { "xy_x", 0.7f }, { "xy_y", 0.4f }, { "comp_amount", 0.2f } },
        {}, {},
        [] { return (Pattern) P (16)
                 .row (K, "X.....x...X..x..")
                 .row (R, "..o...?...o...?.")
                 .row (S, "....X.......X...")
                 .row (C, ".......o.....o..")
                 .row (T, "...........o.oo.")
                 .row (B, "X.....x...X..x..")
                 .row (H, "o.o.x.o.o.o.x.o.")
                 .row (O, "..............x.")
                 .pitch (R, { 0, 0, 7, 0, 0, 0, 12, 0, 0, 0, 5, 0, 0, 0, 3 })
                 .pitch (T, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 3, -2 })
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, -2 }); },
        "Voltage", 128.0f, 0.5f });

    k.push_back ({ "Rubble", "breaks", { "harsh" },
        { { { "Kick", { "kick.hard", "kick.dist" }, { "harsh" } },
            { "Rim", { "snare.rim" }, {}, -3.0f },
            { "Snare", { "snare.crack", "snare.slam" }, {} },
            { "Clap", { "snare.clapstack" }, {}, -2.0f },
            { "Conga", { "perc.conga" }, {}, -4.0f, {}, 0.3f },
            { "Reese", { "bass.reese" }, {}, -1.0f },
            { "Hat", { "hat.closed" }, {}, -1.0f, {}, -0.15f },
            { "Open Hat", { "hat.open" }, {}, -3.0f, {}, 0.15f } } },
        { { "xy_x", 0.55f }, { "xy_y", 0.45f }, { "comp_amount", 0.2f }, { "dist_low_keep", 130.0f } },
        [] (MovementData& m, auto& g) { modulate (m, g, 0, ShapePreset::Triangle, k2Bars, false, vp::FltCutoff, 0.3f, 5); },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X.....x.x.x.....")
                 .row (S, "....X.......X...")
                 .row (R, "..g....g.g....g.")
                 .row (H, "xoxoxoxoxoxoxoxo")
                 .row (O, "......x.......x.")
                 .row (B, "x.....x.x.....x.")
                 .row (T, "...o.......o....")
                 .pitch (B, { 0, 0, 0, 0, 0, 0, -2, 0, 3, 0, 0, 0, 0, 0, 5 })
                 .note (B, 0.9f); },
        "Rubble", 136.0f, 0.5f });

    // Glitch ------------------------------------------------------------------------------
    k.push_back ({ "Fracture", "glitch", { "harsh", "digital" },
        { { { "Kick", { "kick.fold" }, {} },
            { "Blip", { "perc.blip" }, {}, -4.0f, {}, -0.3f },
            { "Snare", { "snare.fold" }, {} },
            { "Clap", { "snare.clapdist" }, {}, -2.0f, { { "drive_type", 3 }, { "drive", 0.5f } } },
            { "Clang", { "perc.clang" }, {}, -4.0f, {}, 0.3f },
            { "Bass", { "bass.growlhard" }, {}, -2.0f },
            { "Hat", { "hat.metalhard" }, {}, -1.0f },
            { "Screech", { "fx.screech" }, {}, -6.0f } } },
        { { "xy_x", 0.85f }, { "xy_y", 0.5f }, { "comp_amount", 0.2f }, { "exc_amount", 0.2f } },
        [] (MovementData& m, auto& g)
        {
            modulate (m, g, 0, ShapePreset::RandomSteps, k1Bar, true, gp::XyX, 0.3f);
            modulate (m, g, 1, ShapePreset::RandomSteps, k2Bars, true, gp::XyY, 0.3f);
        },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X..x.2..X.x..4..")
                 .row (R, "?.?..?..?.?..?.?")
                 .row (S, "....X..3....X.2.")
                 .row (C, "......?....?....")
                 .row (T, "..o..x....3...o.")
                 .row (B, "X.....x..x...x..")
                 .row (H, "4.o.3.o.2.o.o.3.")
                 .row (O, "x.......?.......")
                 .pitch (R, { 0, 0, 12, 0, 0, 7, 0, 0, 3, 0, 10, 0, 0, 5, 0, -2 })
                 .pitch (T, { 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, -2, 0, 0, 0, 5 })
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 3, 0, 0, -2, 0, 0, 0, 5 })
                 .xy (5, 1.0f, 0.9f).xy (13, 0.9f, 1.0f); },
        "Fracture", 124.0f, 0.5f });

    k.push_back ({ "Static", "glitch", { "digital", "organic" },
        { { { "Kick", { "kick.click" }, {} },
            { "Rim", { "snare.rim" }, {}, -3.0f },
            { "Snap", { "snare.snap" }, {}, -2.0f },
            { "Clap", { "snare.clap" }, {}, -3.0f },
            { "Loop", { "texture.loop" }, {}, 0.0f },
            { "Bass", { "bass.pluck" }, {}, -3.0f },
            { "Tick", { "hat.tick" }, {}, 0.0f },
            { "Crackle", { "texture.crackle" }, {}, -5.0f } } },
        { { "xy_x", 0.8f }, { "xy_y", 0.25f }, { "comp_amount", 0.2f }, { "eq_lp", 10000.0f } },
        [] (MovementData& m, auto& g) { modulate (m, g, 0, ShapePreset::RandomSteps, k1Bar, true, gp::XyX, 0.25f); },
        {},
        [] { return (Pattern) P (32)
                 .row (T, "xxxx2xxxxxxx4xxxxxxxxx3xxxxxxx2x")
                 .slices (T, { 0, 1, 2, 3, 3, 5, 6, 7, 0, 9, 10, 11, 12, 13, 1, 15,
                               8, 9, 2, 3, 4, 5, 6, 6, 8, 9, 14, 11, 12, 12, 14, 15 })
                 .row (K, "X......x..X.....X.........x.....")
                 .row (S, "....x.......x.......x.......x...")
                 .row (H, "..?...?...?...?...?...?...?...?.")
                 .row (B, "x.........x.....x.......x.......")
                 .row (O, "x...............................")
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -2 }); },
        "Static", 105.0f, 0.5f });

    // House --------------------------------------------------------------------------------
    k.push_back ({ "Strobe", "house", { "harsh" },
        { { { "Kick", { "kick.hard", "kick.house" }, {} },
            { "Shaker", { "hat.shaker" }, {}, -3.0f, {}, 0.3f },
            { "Snare", { "snare.slam" }, {}, -5.0f },
            { "Clap", { "snare.clapdist" }, {}, 0.0f },
            { "Bell", { "perc.cowbell" }, {}, -5.0f, {}, -0.3f },
            { "Acid", { "bass.acid" }, {}, -2.0f },
            { "Hat", { "hat.hard" }, {}, -1.0f, {}, -0.1f },
            { "Open Hat", { "hat.openhard" }, {}, -1.0f, {}, 0.1f } } },
        { { "xy_x", 0.55f }, { "xy_y", 0.4f }, { "comp_amount", 0.2f }, { "comp_mix", 0.8f } },
        [] (MovementData& m, auto& g) { modulate (m, g, 0, ShapePreset::Triangle, k4Bars, false, vp::FltCutoff, 0.35f, 5); },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X...X...X...X...")
                 .row (R, "gogogogogogogogo")
                 .row (C, "....X.......X...")
                 .row (T, "..x.....x..x....")
                 .row (B, "x.xxo.x.xox.x.xo")
                 .row (H, "g.o.g.o.g.o.g.o.")
                 .row (O, "..x...x...x...x.")
                 .pitch (T, { 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 5 })
                 .pitch (B, { 0, 0, 12, 0, 3, 0, 0, 0, 10, 0, 0, 0, 5, 0, 7, 12 })
                 .note (B, 0.7f); },
        "Strobe", 128.0f, 0.52f });

    k.push_back ({ "Haze", "house", { "warm" },
        { { { "Kick", { "kick.house", "kick.drop" }, { "warm" } },
            { "Rim", { "snare.rim" }, {}, -4.0f, {}, 0.25f },
            { "Snare", { "snare.body" }, {}, -6.0f, { { "flt_cutoff", 4500.0f } } },
            { "Clap", { "snare.clap", "snare.clapstack" }, {}, -2.0f, { { "flt_cutoff", 1100.0f } } },
            { "Chord", { "texture.chord" }, {}, -2.0f, { { "fm_pitch", -12.0f } } },
            { "Sub", { "bass.sub" }, {}, -2.0f },
            { "Shaker", { "hat.shaker" }, {}, -2.0f, {}, -0.25f },
            { "Open Hat", { "hat.open" }, {}, -4.0f, {}, 0.2f } } },
        { { "xy_x", 0.25f }, { "xy_y", 0.2f }, { "comp_amount", 0.2f }, { "eq_lp", 9000.0f } },
        [] (MovementData& m, auto& g)
        {
            modulate (m, g, 0, ShapePreset::Sine, k8Bars, true, gp::EqLp, -0.2f);
            modulate (m, g, 1, ShapePreset::Triangle, k4Bars, false, vp::FltCutoff, 0.25f, 4);
        },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X...X...X...X...")
                 .row (R, "...o......o.....")
                 .row (C, "....x.......x...")
                 .row (T, "x.....x...x.....")
                 .row (B, "..x...x...x..x.o")
                 .row (H, "gogogogogogogogo")
                 .row (O, "..x...x...x...x.")
                 .pitch (T, { 0, 0, 0, 0, 0, 0, -2, 0, 0, 0, 3 })
                 .pitch (B, { 0, 0, 0, 0, 0, 0, -2, 0, 0, 0, 3, 0, 0, 5, 0, 7 }); },
        "Haze", 122.0f, 0.56f });

    // Garage ---------------------------------------------------------------------------------
    k.push_back ({ "Rush", "garage", { "harsh" },
        { { { "Kick", { "kick.hard", "kick.tight" }, {} },
            { "Rim", { "snare.rimhard" }, {}, -3.0f, {}, 0.2f },
            { "Snare", { "snare.slam" }, {} },
            { "Clap", { "snare.clapdist" }, {}, -2.0f },
            { "Stab", { "bass.stab" }, {}, -3.0f, { { "fm_pitch", -12.0f } }, -0.2f },
            { "Reese", { "bass.reese" }, {}, -1.0f, { { "glide", 50.0f } } },
            { "Hat", { "hat.hard", "hat.closed" }, {}, -1.0f, {}, -0.1f },
            { "Open Hat", { "hat.openhard" }, {}, -2.0f, {}, 0.1f } } },
        { { "xy_x", 0.55f }, { "xy_y", 0.4f }, { "comp_amount", 0.2f }, { "dist_low_keep", 140.0f } },
        [] (MovementData& m, auto& g) { modulate (m, g, 0, ShapePreset::Triangle, k2Bars, false, vp::FltCutoff, 0.3f, 5); },
        {},
        [] { return (Pattern) P (16)
                 .row (K, "X...X...X...X...")
                 .row (R, "......o.......o.")
                 .row (S, "....X.......X...")
                 .row (C, "....o.......o...")
                 .row (T, "..x.......x..x..")
                 .row (B, "x..x..x...x.x...")
                 .row (H, "o.x.o.x.o.x.o.x.")
                 .row (O, "..x...x...x...x.")
                 .pitch (T, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, -2 })
                 .pitch (B, { 0, 0, 0, 0, 0, 0, -2, 0, 0, 0, 3, 0, 5 })
                 .note (B, 0.9f); },
        "Rush", 134.0f, 0.6f });

    // Hip hop --------------------------------------------------------------------------------
    k.push_back ({ "Brick", "hip hop", { "harsh" },
        { { { "Kick", { "kick.punch", "kick.hard" }, {} },
            { "Rim", { "snare.rim" }, {}, -3.0f },
            { "Snare", { "snare.slam" }, {} },
            { "Clap", { "snare.clapdist" }, {}, -3.0f },
            { "Tom", { "perc.tomhard" }, {}, -4.0f, {}, 0.3f },
            { "808", { "bass.808clip" }, {}, -2.0f },
            { "Hat", { "hat.hard" }, {}, -2.0f, {}, -0.15f },
            { "Open Hat", { "hat.open" }, {}, -4.0f, {}, 0.15f } } },
        { { "xy_x", 0.35f }, { "xy_y", 0.5f }, { "comp_amount", 0.2f }, { "comp_mix", 0.8f }, { "eq_low", 1.5f } },
        {}, {},
        [] { return (Pattern) P (16)
                 .row (K, "X......xX.x.....")
                 .row (S, "....X.......X...")
                 .row (C, "....o.......o...")
                 .row (R, "..........g....g")
                 .row (T, "..............o.")
                 .row (B, "X......oX.x.....")
                 .row (H, "x.o.x.o.x.o.x.o.")
                 .row (O, "..............o.")
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 0, -2, 0, 0, 3 }); },
        "Brick", 92.0f, 0.58f });

    // Trap --------------------------------------------------------------------------------------
    k.push_back ({ "Riot", "trap", { "harsh" },
        { { { "Kick", { "kick.hard", "kick.punch" }, {} },
            { "Rim", { "snare.rimhard" }, {}, -4.0f },
            { "Snare", { "snare.slam" }, {} },
            { "Snap", { "snare.snap" }, {}, -1.0f },
            { "Zap", { "perc.zaphard" }, {}, -5.0f, {}, 0.3f },
            { "808", { "bass.808clip" }, {}, 0.0f, { { "glide", 80.0f } } },
            { "Hat", { "hat.hard" }, {}, -2.0f, {}, -0.1f },
            { "Open Hat", { "hat.openhard" }, {}, -3.0f, {}, 0.1f } } },
        { { "xy_x", 0.6f }, { "xy_y", 0.45f }, { "comp_amount", 0.2f } },
        {}, {},
        [] { return (Pattern) P (32)
                 .row (K, "X......x..X.....X.x.......X..x..")
                 .row (S, "........x...............x.......")
                 .row (C, "........x...............x.......")
                 .row (H, "x.x.x.x.x.x.x.3.x.x.x.x.x.3.4.4.")
                 .row (O, "......................x.........")
                 .row (B, "X......x..X.....X.x.......X..x..")
                 .row (T, "...o.........o.....o.......o....")
                 .pitch (B, { 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, -2, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, -2, 0, 0, -5 })
                 .note (B, 1.0f); },
        "Riot", 145.0f, 0.5f });

    k.push_back ({ "Smoke", "trap", { "warm" },
        { { { "Kick", { "kick.punch", "kick.tight" }, {} },
            { "Rim", { "snare.rim" }, {}, -4.0f },
            { "Snare", { "snare.clapstack", "snare.body" }, {} },
            { "Clap", { "snare.clapdist" }, {}, -3.0f },
            { "Bell", { "perc.cowbell" }, {}, -3.0f, {}, -0.2f },
            { "808", { "bass.808clip", "bass.808" }, {}, 0.0f, { { "glide", 70.0f } } },
            { "Hat", { "hat.tick", "hat.closed" }, {}, -2.0f },
            { "Open Hat", { "hat.open" }, {}, -4.0f } } },
        { { "xy_x", 0.3f }, { "xy_y", 0.45f }, { "comp_amount", 0.2f }, { "eq_lp", 11000.0f } },
        {}, {},
        [] { return (Pattern) P (16)
                 .row (K, "X.....x...x.....")
                 .row (S, "........x.......")
                 .row (C, "........o.......")
                 .row (T, "x..x..x...x.x...")
                 .row (B, "X.....x...x.....")
                 .row (H, "x.x.x.x.x.x.3.x.")
                 .row (O, "..............o.")
                 .pitch (T, { 0, 0, 0, 3, 0, 0, 7, 0, 0, 0, 5, 0, 3 })
                 .pitch (B, { 0, 0, 0, 0, 0, 0, -2, 0, 0, 0, 3 })
                 .note (B, 1.0f); },
        "Smoke", 135.0f, 0.5f });

    return k;
}
} // namespace

const std::vector<KitConcept>& kitConcepts()
{
    static const auto list = build();
    return list;
}

// The B patterns: a build, a break or a fill for each of the later kits, so
// their sets have somewhere to go.
const std::vector<ExtraPattern>& extraPatterns()
{
    static const std::vector<ExtraPattern> list {
        { "Pressure B", "techno", [] { return (Pattern) P (32)
              .row (K, "X...X...X...X...X...X...X...X.X.")
              .row (S, "................x...x...x.x.2244")
              .row (C, "....x.......x.......x.......x...")
              .row (H, "xoxoxoxoxoxoxoxoxxxxxxxxxxxxxxxx")
              .row (B, "..x...x...x...x...x...x...x...x.")
              .xy (16, 0.6f, 0.55f).xy (20, 0.65f, 0.65f).xy (24, 0.7f, 0.75f).xy (28, 0.8f, 0.85f).xy (30, 0.9f, 0.95f); } },
        { "Furnace B", "techno", [] { return (Pattern) P (32)
              .row (K, "X...X...X...X...X...X...X.X.XXXX")
              .row (C, "....x.......x.......x.......x...")
              .row (H, "..x...x...x...x...x...x...x...x.")
              .row (B, "x.......x.......x...x...x.x.x.xx")
              .pitch (B, { 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 5, 0, 7, 0, 10, 0, 12, 12 }); } },
        { "Tunnel B", "techno", [] { return (Pattern) P (32)
              .row (K, "................X...X...X...X...")
              .row (B, "..x...x...x...x...x...x...x...x.")
              .row (T, "x..o..x")
              .row (O, "x...x...x...x...x...x...x...x...")
              .row (H, "................g.o.g.o.g.o.g.o.")
              .length (T, 7)
              .pitch (T, { 0, 0, 0, 5, 0, 0, -2 }); } },
        { "Splinter B", "breaks", [] { return (Pattern) P (32)
              .row (K, "X.x...x...x.x...X...x.x...x.x...")
              .row (S, "....X..2..g.X.2.....X.4...X.4444")
              .row (C, "..g...g...3...g...g...2...3.....")
              .row (H, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.")
              .row (B, "X.......x.......X...x.......x...")
              .pitch (B, { 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -2, 0, 0, 0, 0, 0, 0, 0, 5 })
              .note (B, 0.9f); } },
        { "Voltage B", "breaks", [] { return (Pattern) P (32)
              .row (K, "X.....x...X..x..X.....x.x.X.....")
              .row (R, "..o...?...o...?...o...?...3...4.")
              .row (S, "....X.......X.......X.......X...")
              .row (T, "...........o.oo...........o.oo.o")
              .row (B, "X.....x...X..x..X.....x.x.X.....")
              .row (H, "o.o.x.o.o.o.x.o.o.o.x.o.o.o.x.o.")
              .pitch (R, { 0, 0, 7, 0, 0, 0, 12, 0, 0, 0, 5, 0, 0, 0, 3, 0, 0, 0, 7, 0, 0, 0, 12, 0, 0, 0, 10, 0, 0, 0, 15 })
              .pitch (B, { 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, -2, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 3, 0, 0 }); } },
        { "Rubble B", "breaks", [] { return (Pattern) P (32)
              .row (K, "X.....x.x.x.....X.....x...x.x.x.")
              .row (S, "....X.......X.......X.......X.2.")
              .row (R, "..g....g.g....g...g....g.g..g.g.")
              .row (H, "xoxoxoxoxoxoxoxo................")
              .row (O, "......x.......x.x...x...x...x...")
              .row (B, "x.....x.x.....x.x...............")
              .pitch (B, { 0, 0, 0, 0, 0, 0, -2, 0, 3, 0, 0, 0, 0, 0, 5, 0, 7 })
              .note (B, 0.9f)
              .xy (24, 0.7f, 0.7f).xy (28, 0.8f, 0.85f); } },
        { "Fracture B", "glitch", [] { return (Pattern) P (32)
              .row (K, "X.2.X..4X.x.3...X..x.2..X.x..444")
              .row (S, "....X..3....X.2.....X.4.....X.33")
              .row (T, "..o..x....3...o...3..x..4...o...")
              .row (H, "4.o.3.o.2.o.o.3.4.3.2.3.4.3.2.4.")
              .row (B, "X.....x..x...x..X...x.x..x..x...")
              .row (O, "x...............x.......?.......")
              .pitch (T, { 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, -2, 0, 0, 0, 5, 0, 0, 0, 7, 0, 0, 3, 0, 0, 12, 0, 0, 0, 5 })
              .pitch (B, { 0, 0, 0, 0, 0, 0, 3, 0, 0, -2, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 7, 0, 3, 0, 0, -2, 0, 0, 5 })
              .xy (8, 1.0f, 1.0f).xy (24, 0.2f, 0.9f).xy (30, 1.0f, 1.0f); } },
        { "Static B", "glitch", [] { return (Pattern) P (32)
              .row (T, "x.x.x.x.xxxx2222x.x.x.x.44443333")
              .slices (T, { 0, 0, 2, 0, 4, 0, 6, 0, 8, 9, 10, 11, 12, 12, 12, 12,
                            0, 0, 2, 0, 4, 0, 6, 0, 3, 3, 3, 3, 7, 7, 7, 7 })
              .row (K, "X.........x.....X.........x.....")
              .row (H, "..?...?...?...?...?...?...?...?.")
              .row (O, "x...............x...............")
              .row (B, "x.........x.....x.......x.......")
              .pitch (B, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5 }); } },
        { "Strobe B", "house", [] { return (Pattern) P (32)
              .row (K, "................X...X...X...X...")
              .row (R, "gogogogogogogogogogogogogogogogo")
              .row (C, "....X.......X.......X.......X.2.")
              .row (B, "x.xxo.x.xox.x.xox.xxo.x.xox.x.xo")
              .row (O, "..x...x...x...x...x...x...x...x.")
              .pitch (B, { 0, 0, 12, 0, 3, 0, 0, 0, 10, 0, 0, 0, 5, 0, 7, 12, 0, 0, 12, 15, 3, 0, 0, 0, 10, 12, 0, 0, 5, 0, 7, 12 })
              .note (B, 0.7f)
              .xy (0, 0.5f, 0.2f).xy (8, 0.5f, 0.3f).xy (16, 0.55f, 0.45f).xy (24, 0.6f, 0.6f).xy (30, 0.7f, 0.8f); } },
        { "Haze B", "house", [] { return (Pattern) P (32)
              .row (K, "X...X...X...X...X...X...X...X...")
              .row (R, "...o......o.......o.....o..o....")
              .row (C, "....x.......x.......x.......x...")
              .row (T, "x.....x...x.....x.....x...x..x..")
              .row (B, "..x...x...x..x.o..x...x...x..x.o")
              .row (H, "gogogogogogogogogogogogogogogogo")
              .row (O, "..x...x...x...x...x...x...x...x.")
              .pitch (T, { 0, 0, 0, 0, 0, 0, -2, 0, 0, 0, 3, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 3, 0, 0, 0, -2, 0, 0, 0 })
              .pitch (B, { 0, 0, 0, 0, 0, 0, -2, 0, 0, 0, 3, 0, 0, 5, 0, 7, 0, 0, 5, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, -2, 0, 0 }); } },
        { "Rush B", "garage", [] { return (Pattern) P (32)
              .row (K, "X.........X.....X......x..X.....")
              .row (R, "....x..o....x.......x..o....x...")
              .row (S, "....X.......X.......X.......X...")
              .row (T, "..x..x.....x..........x..x......")
              .row (B, "x..x..x...x.x...x..x..x...x..x..")
              .row (H, "..o.?.o...o.?.o...o.?.o...o.?.o.")
              .row (O, "..............x...............x.")
              .pitch (T, { 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, -2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 3 })
              .pitch (B, { 0, 0, 0, 0, 0, 0, -2, 0, 0, 0, 3, 0, 5, 0, 0, 0, 0, 0, 0, 3, 0, 0, 7, 0, 0, 0, 5, 0, 0, -2 })
              .note (B, 0.9f); } },
        { "Brick B", "hip hop", [] { return (Pattern) P (32)
              .row (K, "X......xX.x.....X.....x...x.....")
              .row (S, "....X.......X.......X.......X...")
              .row (R, "..........g....g..........g..g.g")
              .row (T, "..............o.............o.oo")
              .row (B, "X......oX.x.....X.....o...o.....")
              .row (H, "x.o.x.o.x.o.x.o.x.o.x.o.x.2.x.2.")
              .row (O, "..............o...............o.")
              .pitch (T, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, -2 })
              .pitch (B, { 0, 0, 0, 0, 0, 0, 0, -2, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 3 }); } },
        { "Riot B", "trap", [] { return (Pattern) P (32)
              .row (K, "X.........X.....X.x.......X.x.x.")
              .row (S, "........x...............x.....x.")
              .row (C, "........x...............x.......")
              .row (H, "x..x..x.x..x..x.3.3.3.3.4.4.4.4.")
              .row (B, "X.........X.....X.x.......X.x.x.")
              .row (T, "...........................3....")
              .pitch (B, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, -2, 0, -5, 0, -7 })
              .note (B, 1.0f); } },
        { "Smoke B", "trap", [] { return (Pattern) P (32)
              .row (K, "X.....x...x.....X...x.....x.x...")
              .row (S, "........x...............x.......")
              .row (C, "........o...............o.....o.")
              .row (T, "x..x..x...x.x...x..x..x...x.x.xx")
              .row (B, "X.....x...x.....X...x.....x.....")
              .row (H, "x.x.x.x.x.x.3.x.x.x.x.x.3.3.4.4.")
              .row (O, "..............o...............o.")
              .pitch (T, { 0, 0, 0, 3, 0, 0, 7, 0, 0, 0, 5, 0, 3, 0, 0, 0, 0, 0, 0, 3, 0, 0, 10, 0, 0, 0, 7, 0, 5, 0, 3, 0 })
              .pitch (B, { 0, 0, 0, 0, 0, 0, -2, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 3 })
              .note (B, 1.0f); } },
    };
    return list;
}

const std::vector<SetSpec>& setSpecs()
{
    static const std::vector<SetSpec> sets {
        { "Neutral Breakbeat", "Neutral", { "Breakbeat" }, 120.0f, 0.5f, { "warm", "organic" } },
        { "Concrete", "Concrete", { "Concrete", "Foundry", "Sparse", "Solvent" }, 130.0f, 0.5f, { "techno" } },
        { "Reel", "Reel", { "Reel", "Tension", "Vector", "Breakbeat" }, 130.0f, 0.5f, { "breaks" } },
        { "Bitrate", "Bitrate", { "Bitrate", "Shards", "Alloy" }, 120.0f, 0.5f, { "glitch" } },
        { "Floor", "Floor", { "Floor", "Velvet", "Rolling" }, 124.0f, 0.54f, { "house" } },
        { "Shuffle", "Shuffle", { "Shuffle", "Weight" }, 132.0f, 0.62f, { "garage" } },
        { "Sepia", "Sepia", { "Sepia", "Faded" }, 90.0f, 0.6f, { "hip hop" } },
        { "Slide", "Slide", { "Slide", "Blackout" }, 140.0f, 0.5f, { "trap" } },
        { "Pressure", "Pressure", { "Pressure", "Pressure B", "Furnace", "Tunnel" }, 140.0f, 0.5f, { "techno", "harsh" } },
        { "Splinter", "Splinter", { "Splinter", "Splinter B", "Rubble", "Voltage" }, 172.0f, 0.5f, { "breaks", "harsh" } },
        { "Fracture", "Fracture", { "Fracture", "Fracture B", "Static", "Static B" }, 124.0f, 0.5f, { "glitch", "harsh" } },
        { "Strobe", "Strobe", { "Strobe", "Strobe B", "Haze", "Haze B" }, 128.0f, 0.52f, { "house", "harsh" } },
        { "Rush", "Rush", { "Rush", "Rush B", "Shuffle", "Weight" }, 134.0f, 0.6f, { "garage", "harsh" } },
        { "Brick", "Brick", { "Brick", "Brick B", "Sepia", "Faded" }, 92.0f, 0.58f, { "hip hop", "harsh" } },
        { "Riot", "Riot", { "Riot", "Riot B", "Smoke", "Smoke B" }, 145.0f, 0.5f, { "trap", "harsh" } },
    };
    return sets;
}

} // namespace batida::factory
