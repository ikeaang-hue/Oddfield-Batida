#pragma once

#include "Engine/Parameters.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>

namespace batida
{

constexpr int kNumPatterns = 16;
constexpr int kMaxSteps = 64;
constexpr int kMinPatternSteps = 8;
constexpr int kNumTracks = kNumVoices;
constexpr int kMaxRatchet = 4;
constexpr int kMaxSlices = 32;

struct Step
{
    bool gate = false;
    uint8_t velocity = 100;   // 1..127
    int8_t pitch = 0;         // semitones, -24..24
    uint8_t slice = 0;        // 0-based
    uint8_t ratchet = 1;      // 1..4
    uint8_t probability = 100; // 0..100 %
};

struct XyLock
{
    bool active = false;
    float x = 0.5f, y = 0.0f;
};

struct Track
{
    std::array<Step, kMaxSteps> steps {};
    int length = 16;          // 1..pattern length (polymeter)
    float noteLength = 0.5f;  // fraction of a step, for Gate-mode voices
};

struct Pattern
{
    int length = 16;          // 8..64 steps of a 16th note
    std::array<Track, kNumTracks> tracks {};
    std::array<XyLock, kMaxSteps> xy {};

    bool isEmpty() const;
    void clear();
};

struct PatternBank
{
    std::array<Pattern, kNumPatterns> patterns {};

    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement& xml); // missing parts keep their defaults
};

// One pattern as XML (the same form the project uses), for pattern files.
std::unique_ptr<juce::XmlElement> patternToXml (const Pattern& pattern);
void patternFromXml (const juce::XmlElement& xml, Pattern& pattern);

// The breakbeat for the default kit (a factory pattern, and the dev default).
Pattern breakbeatPattern();

// Every pattern starts empty, as in a release. Built with -DBATIDA_DEMO=ON,
// pattern 1 starts with the breakbeat demo (development).
#ifndef BATIDA_DEMO
 #define BATIDA_DEMO 0
#endif
constexpr bool kShipDemoPattern = BATIDA_DEMO != 0;
PatternBank defaultPatternBank();

} // namespace batida
