#pragma once

#include "Look/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Small drawn graphs that stand in for help text: the shape of an envelope, a
// filter's response, the drive's transfer curve, an FM algorithm's routing, a
// waveform. Each takes plain values and repaints only when they change.

// The frame the graphs share: black, 1 px outline, a faint baseline.
void drawGraphFrame (juce::Graphics& g, juce::Rectangle<float> r);

// Amp envelope: attack, decay (to −60 dB), sustain, release; in Gate mode the
// sustain holds before the release.
class AmpEnvelopeGraph final : public juce::Component
{
public:
    void set (float attackMs, float decayMs, float sustain, float releaseMs, bool gate);
    void paint (juce::Graphics&) override;

private:
    std::array<float, 5> values { -1.0f, 0, 0, 0, 0 };
};

// Pitch envelope: a start offset in semitones that decays to zero.
class PitchEnvelopeGraph final : public juce::Component
{
public:
    void set (float semitones, float decayMs);
    void paint (juce::Graphics&) override;

private:
    float amount = -1000.0f, decay = 0.0f;
};

// A tiny ADSR for an FM operator card (the first second).
class MiniEnvelope final : public juce::Component
{
public:
    void set (float attackMs, float decayMs, float sustain, float releaseMs, bool dim = false);
    void paint (juce::Graphics&) override;

private:
    std::array<float, 4> values { -1.0f, 0, 0, 0 };
    bool dimmed = false;
};

// Filter response on a log frequency axis, 20 Hz to 20 kHz.
class FilterGraph final : public juce::Component
{
public:
    void set (int type, float cutoffHz, float resonance); // type: 0 LP, 1 HP, 2 BP
    void paint (juce::Graphics&) override;

private:
    int filterType = -1;
    float cutoff = 0.0f, res = 0.0f;
};

// Drive transfer curve: soft, hard, fold or crush, at an amount.
class DriveGraph final : public juce::Component
{
public:
    void set (int type, float amount);
    void paint (juce::Graphics&) override;

private:
    int driveType = -1;
    float drive = 0.0f;
};

// The operator routing of one FM algorithm: carriers filled lime on the
// bottom row, modulators outlined above what they modulate, feedback on op 4.
class AlgorithmView final : public juce::Component
{
public:
    void set (int algorithm);
    void paint (juce::Graphics&) override;

private:
    int algorithm = -1;
};

// A waveform from peak levels (0..1): the Vary tiles and the layer's sample.
class PeaksView final : public juce::Component
{
public:
    void set (std::vector<float> peaks, juce::Colour colour);
    void setPlayhead (float position); // 0..1, < 0 = none
    void paint (juce::Graphics&) override;

private:
    std::vector<float> peaks;
    juce::Colour colour = theme::ink;
    float playhead = -1.0f;
};

// An FM operator's waveform: sine through triangle, saw, square to noise.
class WaveGlyph final : public juce::Component
{
public:
    void set (float wave);
    void paint (juce::Graphics&) override;

private:
    float value = -1.0f;
};

juce::String waveName (float wave); // "Sine", "Tri", "Saw", "Square", "Noise" and in-betweens
