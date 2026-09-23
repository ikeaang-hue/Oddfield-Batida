#pragma once

#include "Engine/Movement/Movement.h"
#include "Engine/Parameters.h"
#include "Engine/Sequencer/Pattern.h"

#include <juce_core/juce_core.h>

#include <optional>

// Library files: one sound, a kit, one pattern, or a whole set. Readable XML,
// values stored by parameter key in their own units (a knob reads the same
// whatever its range is later), samples both relative to the file and as an
// absolute path.

namespace batida
{

enum class PresetType { Sound, Kit, Pattern, Set };
constexpr int kNumPresetTypes = 4;

constexpr int kPresetFormat = 1; // bump when the file layout changes

const char* extensionFor (PresetType type);  // ".batida-sound" ...
const char* typeName (PresetType type);      // "sound", "kit", "pattern", "set"
std::optional<PresetType> presetTypeOf (const juce::File& file);

const juce::StringArray& soundCategories();  // kick, snare, hat, perc, fx, bass, texture
const juce::StringArray& characterTags();    // warm, harsh, digital, metallic, organic
juce::String guessCategory (const juce::String& soundName);

struct PresetInfo
{
    juce::String name, author, category; // category: one of soundCategories(), or empty
    juce::StringArray tags;
    juce::String madeWith;                // the Batida version that wrote it
    int format = kPresetFormat;
};

struct SampleRef
{
    juce::String path;    // absolute; after reading, where it was found (or last known, if missing)
    juce::int64 bytes = -1;
    bool isEmpty() const { return path.isEmpty(); }
};

struct SoundPreset
{
    PresetInfo info;
    VoiceParams params;
    SampleRef sample;
};

// The kit globals: the pad, the chain, the modulators and scenes. Not MIDI,
// the sequencer or Master, which belong to the project.
bool isKitGlobal (int param);

struct KitPreset
{
    PresetInfo info;
    std::array<VoiceParams, kNumVoices> voices {};
    std::array<juce::String, kNumVoices> names;
    std::array<SampleRef, kNumVoices> samples;
    std::array<float, kNumGlobalParams> globals {}; // kit files hold the kit globals; sets hold all
    MovementData movement;
};

struct PatternPreset
{
    PresetInfo info;
    Pattern pattern;
};

struct SetPreset
{
    PresetInfo info;
    KitPreset kit; // with every global
    PatternBank patterns;
};

// Writing: samples are stored relative to the file's folder (and absolute).
bool writeSound (const juce::File& file, const SoundPreset& preset, juce::String* error = nullptr);
bool writeKit (const juce::File& file, const KitPreset& preset, juce::String* error = nullptr);
bool writePattern (const juce::File& file, const PatternPreset& preset, juce::String* error = nullptr);
bool writeSet (const juce::File& file, const SetPreset& preset, juce::String* error = nullptr);

// Reading: each sample path is resolved (relative to the file first, then the
// absolute path); a sample found in neither place keeps its absolute path.
// Files from a newer format still open: what this version knows is read.
std::optional<SoundPreset> readSound (const juce::File& file, juce::String* error = nullptr);
std::optional<KitPreset> readKit (const juce::File& file, juce::String* error = nullptr);
std::optional<PatternPreset> readPattern (const juce::File& file, juce::String* error = nullptr);
std::optional<SetPreset> readSet (const juce::File& file, juce::String* error = nullptr);

std::optional<PresetInfo> readInfo (const juce::File& file); // for the library index

// A name that's safe as a file name ("Kick / 909?" -> "Kick - 909").
juce::String safeFileName (const juce::String& name);

// The default kit and sounds as presets (the factory's neutral kit, and Init).
KitPreset defaultKitPreset();
SoundPreset initSoundPreset(); // a plain starting sound

} // namespace batida
