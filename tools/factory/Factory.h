#pragma once

#include "Engine/Kit.h"
#include "Engine/Movement/Vary.h"
#include "Library/PresetFiles.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The preset factory (SPEC §11): an offline tool on the same engine as the
// plugin. Recipes make sound candidates, which are rendered, measured,
// tagged, named and filtered; the kept ones are assembled into kits, and the
// kits get hand-written patterns and sets.
//
// Everything is deterministic: a candidate is its recipe plus a seed, so the
// whole library can be made again after an engine change.

namespace batida::factory
{

constexpr double kRate = 48000.0;
using Overrides = std::vector<std::pair<const char*, float>>;

void apply (VoiceParams& p, const Overrides& o);        // by parameter key; asserts every key exists
VoiceParams voiceFrom (const Overrides& o);            // the parameter defaults, then `o`
void applyGlobals (std::array<float, kNumGlobalParams>& g, const Overrides& o);

// Analysis -------------------------------------------------------------------

struct Features
{
    float peakDb = -120.0f, loudnessDb = -120.0f; // loudness: the loudest 50 ms
    float lengthMs = 0.0f;                        // until -60 dB below the peak
    float centroidHz = 0.0f;                      // spectral centroid
    float low = 0.0f, high = 0.0f;                // share of energy below 150 Hz, above 6 kHz
    float flatness = 0.0f;                        // 0 tonal .. 1 white noise (200 Hz..16 kHz)
    float inharmonic = 0.0f;                      // 0 harmonic .. 1 clangorous (tonal parts)
    float pitchHz = 0.0f;                         // after the attack, from zero crossings (tonal sounds)
    float crestDb = 0.0f;                         // peak over loudness: how spiky the attack is
    bool finite = true;
};

// One hit, rendered dry (the voice alone), mono sum. Gate sounds get a note
// of `gateSeconds`.
std::vector<float> renderHit (const VoiceParams& p, const SampleData* sample, double seconds = 2.0,
                              double gateSeconds = 0.25, int key = Voice::kBaseKey);
Features analyse (const std::vector<float>& mono);
Features analyse (const VoiceParams& p, const SampleData* sample);

// A distance between two sounds in rough "just noticeable" units.
float distance (const Features& a, const Features& b);

// Sounds ---------------------------------------------------------------------

struct Range
{
    const char* key;
    float lo, hi;
    bool log = false; // times and frequencies: spread evenly on a log scale
};

// Engine-made audio for a sample-based recipe: rendered by the engine from
// the seed (a resampled hit, a loop, crackle...).
using SampleMaker = std::function<juce::AudioBuffer<float> (uint32_t seed)>;

struct Archetype
{
    std::string id;       // "kick.drop"
    std::string category; // one of soundCategories()
    std::vector<std::string> nouns; // for names: "Kick", "Drop"
    Overrides base;
    std::vector<Range> ranges;
    int candidates = 10;  // how many to make
    int keep = 3;         // how many the factory takes (before review)
    std::vector<std::string> hints; // character tags the recipe aims for
    SampleMaker sample;   // set for Sample and Layer sounds
    bool loop = false;    // the sample is a loop (for Slice or looping)
};

const std::vector<Archetype>& archetypes();
const Archetype* findArchetype (const std::string& id);

// Per-category loudness targets (dB, the loudest 50 ms, dry) and dud rules.
float loudnessTarget (const std::string& archetypeId);
bool fitsCategory (const Archetype& a, const Features& f, juce::String* why = nullptr);
juce::StringArray guessTags (const Archetype& a, const VoiceParams& p, const Features& f);

struct Candidate
{
    std::string archetype;
    int index = 0;             // within its archetype
    uint32_t seed = 0;
    VoiceParams params;
    Features features;
    juce::String name;
    juce::StringArray tags;
    juce::AudioBuffer<float> sampleAudio; // Sample and Layer sounds
    std::shared_ptr<SampleSlot> sample;   // the same audio, loaded as the engine would
    float score = 0.0f;                   // auto-pick order within the archetype (higher first)
    Features raw;                         // before levelling
    juce::String dropped;                 // why it never reaches review (empty = fine)
};

// Makes every candidate of every archetype, with duds marked as dropped.
std::vector<Candidate> makeCandidates();

// Names: descriptive, unique within a category.
void nameCandidates (std::vector<Candidate*>& list);

// Kits, patterns and sets ------------------------------------------------------

struct SlotSpec
{
    const char* name;                      // the slot's name in the kit
    std::vector<std::string> archetypes;   // any of these, in order of preference
    std::vector<std::string> prefer;       // tags that make a better fit
    float trimDb = 0.0f;                   // level in the kit, relative to the pool's level
    Overrides tweak;                       // kit-specific changes on top of the chosen sound
    float pan = 0.0f;
};

struct KitConcept
{
    std::string name;
    std::string style;                    // a tag: techno, breaks, glitch, house, garage, hip hop, trap
    juce::StringArray tags;               // character tags
    std::array<SlotSpec, kNumVoices> slots;
    Overrides chain;                      // kit globals
    std::function<void (MovementData&, std::array<float, kNumGlobalParams>&)> movement; // mods (optional)
    std::array<Overrides, kNumScenes> scenes; // offsets over the chain for B, C, D (A = the chain)
    std::function<Pattern()> pattern;
    std::string patternName;
    float tempo = 120.0f, swing = 0.5f;
    bool neutral = false;                 // the startup kit, taken as it is
};

const std::vector<KitConcept>& kitConcepts();

struct SetSpec
{
    std::string name;
    std::string kit;                      // a kit concept's name
    std::vector<std::string> patterns;    // pattern names, into slots 1..n
    float tempo = 120.0f, swing = 0.5f;
    juce::StringArray tags;
};

const std::vector<SetSpec>& setSpecs();

// Renders a kit (with its samples) playing `pattern` for `bars` at `tempo`
// from the Kit engine (chain, master included), stereo.
juce::AudioBuffer<float> renderKit (const KitPreset& kit, const std::array<std::shared_ptr<SampleSlot>, kNumVoices>& samples,
                                    const Pattern& pattern, float tempo, float swing, int bars,
                                    const Overrides& globals = {});

// Loudness of a rendered beat: RMS over everything after the first bar, dB.
float beatLoudnessDb (const juce::AudioBuffer<float>& audio);
float peakDb (const juce::AudioBuffer<float>& audio);

// Output ---------------------------------------------------------------------

bool writeFlac (const juce::File& file, const juce::AudioBuffer<float>& audio);
void stripAbsolutePaths (const juce::File& presetFile); // factory files: relative sample paths only

} // namespace batida::factory
