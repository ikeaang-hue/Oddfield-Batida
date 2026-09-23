#pragma once

#include "Engine/Kit.h"

// Shared helpers for the test program: offline rendering and measurements.
namespace batida::tools
{

struct Stats
{
    float peakDb = -120.0f;
    float rmsDb = -120.0f;
    float lengthMs = 0.0f;     // until the level stays below -60 dB of peak
    float centroidHz = 0.0f;   // brightness estimate from zero crossings
    bool finite = true;
};

// Renders one voice of the given kit, triggered once at t = 0.
juce::AudioBuffer<float> renderVoice (const KitParams& params, int voice, int key, float velocity,
                                      double seconds, double sampleRate = 48000.0,
                                      double noteLengthSeconds = 0.1);

Stats measure (const juce::AudioBuffer<float>& audio, double sampleRate);

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate);

// A simple two-bar beat at 120 bpm (kick, snare, hats, clap) from MIDI,
// played `repeats` times.
juce::AudioBuffer<float> renderLoop (const KitParams& params, int repeats = 1, double sampleRate = 48000.0);

// RMS and peak (dB) of the last `seconds` of a buffer, both channels.
std::pair<float, float> levelOfTail (const juce::AudioBuffer<float>& audio, double seconds, double sampleRate = 48000.0);

int runRender (const juce::File& outDir);
int runPad (const juce::File& outDir);
int runBeat (const juce::File& outDir);
int runBenchmark();

} // namespace batida::tools
