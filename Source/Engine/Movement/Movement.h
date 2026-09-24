#pragma once

#include "Engine/Parameters.h"

#include <juce_core/juce_core.h>

#include <array>

namespace batida
{

constexpr int kMaxModPoints = 32;
constexpr int kMaxModTargets = 8;
constexpr int kNumScenes = 4;

// A modulator's drawn shape: points over one cycle (x 0..1, y 0..1); `curve`
// bends the segment that starts at the point (-1..1, 0 = straight).
struct ModPoint
{
    float x = 0.0f, y = 0.0f, curve = 0.0f;
};

// Where a modulator goes: a voice parameter or a kit parameter, with a depth
// in knob travel (-1..1 of the whole range).
struct ModTarget
{
    bool active = false;
    bool global = false;
    int voice = 0, param = 0;
    float depth = 0.25f;

    bool operator== (const ModTarget& o) const
    {
        return active == o.active && global == o.global && (global || voice == o.voice) && param == o.param;
    }
};

struct ModSetup
{
    std::array<ModPoint, kMaxModPoints> points {};
    int numPoints = 0;
    std::array<ModTarget, kMaxModTargets> targets {};

    bool hasTargets() const;
};

// A stored chain state: the values of sceneParams().
struct Scene
{
    bool stored = false;
    std::array<float, kNumGlobalParams> values {};
};

struct MovementData
{
    std::array<ModSetup, kNumMods> mods {};
    std::array<Scene, kNumScenes> scenes {};

    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement& xml);
};

enum class ShapePreset { Sine, Triangle, RampUp, RampDown, Square, RandomSteps };
void setPreset (ModSetup& setup, ShapePreset preset);

// Value of the shape at phase 0..1 (wraps back to the first point).
float evaluateShape (const ModSetup& setup, float phase);

// The kit parameters a scene holds: the pad and every chain stage.
const std::vector<int>& sceneParams();

// Which parameters a modulator may drive: continuous voice parameters, and
// the pad, the chain stages, the master and the scene morph.
bool isModulatableGlobal (int param);
bool isModulatableVoice (int param);

// Both modulators start empty, as in a release. Built with -DBATIDA_DEMO=ON,
// Mod 1 slowly moves XY Heat (development).
#ifndef BATIDA_DEMO
 #define BATIDA_DEMO 0
#endif
constexpr bool kShipDemoModulation = BATIDA_DEMO != 0;
MovementData defaultMovement();

} // namespace batida
