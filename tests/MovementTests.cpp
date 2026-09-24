#include "Tools.h"

#include "Engine/Movement/Vary.h"
#include "Engine/ParamRange.h"

#include <cmath>

namespace batida
{

namespace
{
constexpr double kRate = 48000.0;

KitParams quietKit()
{
    auto k = defaultKitParams();
    k.global[gp::XyY] = 0.0f;
    return k;
}
} // namespace

class MovementTests final : public juce::UnitTest
{
public:
    MovementTests() : juce::UnitTest ("Movement", "Batida") {}

    void runTest() override
    {
        beginTest ("Shapes: points, wrap-around and curves");
        {
            ModSetup s;
            setPreset (s, ShapePreset::Triangle);
            expectWithinAbsoluteError (evaluateShape (s, 0.0f), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (evaluateShape (s, 0.25f), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (evaluateShape (s, 0.5f), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (evaluateShape (s, 0.75f), 0.5f, 1.0e-6f); // back towards point 0
            s.points[0].curve = 1.0f;
            expectLessThan (evaluateShape (s, 0.25f), 0.2f, "a positive curve starts slow");
            setPreset (s, ShapePreset::Sine);
            expectWithinAbsoluteError (evaluateShape (s, 0.25f), 1.0f, 1.0e-6f);
            expectGreaterThan (evaluateShape (s, 0.125f), 0.8f, "sine rises fast through the middle");
        }

        beginTest ("Sync follows the clock: a 1-bar shape lines up with bars");
        {
            Modulators m;
            m.prepare (kRate);
            auto g = defaultKitParams().global;
            g[modParam (0, ModRate)] = 11.0f; // 1 bar
            MovementData d;
            setPreset (d.mods[0], ShapePreset::RampUp);
            m.advance (32, 2.0, 1.0e-5, g, d);  // half a bar in
            expectWithinAbsoluteError (m.value (0), 0.5f, 0.01f);
            m.advance (32, 6.0, 1.0e-5, g, d);  // half-way through bar 2
            expectWithinAbsoluteError (m.value (0), 0.5f, 0.01f);
            g[modParam (0, ModPolarity)] = 1.0f;
            m.advance (32, 3.0, 1.0e-5, g, d);
            expectWithinAbsoluteError (m.value (0), 0.5f, 0.01f, "bipolar: 0.75 of the ramp = +0.5");
            g[modParam (0, ModAmount)] = 0.5f;
            m.advance (32, 3.0, 1.0e-5, g, d);
            expectWithinAbsoluteError (m.value (0), 0.25f, 0.01f, "Amount scales the output");
        }

        beginTest ("One-shot runs once per trigger, then holds");
        {
            Modulators m;
            m.prepare (kRate);
            auto g = defaultKitParams().global;
            g[modParam (0, ModMode)] = 2.0f;    // one-shot
            g[modParam (0, ModRate)] = 8.0f;    // a quarter note
            g[modParam (0, ModTrigger)] = 2.0f; // voice 1
            MovementData d;
            setPreset (d.mods[0], ShapePreset::RampUp);
            const auto pps = 120.0 / 60.0 / kRate; // 120 bpm
            m.advance (32, 0.0, pps, g, d);
            expectWithinAbsoluteError (m.value (0), 0.0f, 1.0e-4f, "waits for its trigger");
            m.hit (3);
            m.advance (32, 0.0, pps, g, d);
            expectWithinAbsoluteError (m.value (0), 0.0f, 0.02f, "another voice doesn't trigger it");
            m.hit (0);
            for (int i = 0; i < 375; ++i) // half a quarter note at 120 bpm = 12000 samples
                m.advance (32, 0.0, pps, g, d);
            expectWithinAbsoluteError (m.value (0), 0.5f, 0.02f);
            for (int i = 0; i < 1000; ++i)
                m.advance (32, 0.0, pps, g, d);
            expectGreaterThan (m.value (0), 0.98f, "holds the end");
        }

        beginTest ("Modulation offsets move a value like turning its knob");
        for (int p = 0; p < kNumVoiceParams; ++p)
        {
            if (! isModulatableVoice (p))
                continue;
            const auto& r = voiceRanges()[(size_t) p];
            const auto base = r.convertFrom0to1 (0.3f);
            const auto moved = r.convertFrom0to1 (std::clamp (r.convertTo0to1 (base) + 0.25f, 0.0f, 1.0f));
            expectWithinAbsoluteError (r.convertTo0to1 (moved), 0.55f, 0.02f, voiceParamSpecs()[(size_t) p].key);
        }

        beginTest ("Through the kit: a modulator on Heat changes the chain");
        {
            auto params = quietKit();
            Kit kit;
            auto& store = kit.movementStore();
            store.edit ([] (MovementData& d)
            {
                setPreset (d.mods[0], ShapePreset::Square); // high for the first half, then low
                d.mods[0].targets[0] = { true, true, 0, gp::DistDrive, 1.0f };
            });
            kit.setParameters (params);
            kit.prepare (kRate, 256);
            juce::AudioBuffer<float> buf (2, 256);
            float firstHalf = 0.0f, secondHalf = 0.0f;
            // Internal clock at 120 bpm: 1 bar = 2 s. Hold a sustained bass.
            kit.noteOn (5, 60, 1.0f);
            for (int b = 0; b < 375; ++b)
            {
                kit.setParameters (params);
                kit.process (buf, {});
                const auto crest = buf.getMagnitude (0, 0, 256) / std::max (1.0e-6f, buf.getRMSLevel (0, 0, 256));
                (b < 187 ? firstHalf : secondHalf) += crest;
            }
            logMessage ("  crest factor with drive modulated high / low: " + juce::String (firstHalf / 187, 2) + " / "
                        + juce::String (secondHalf / 188, 2));
            expect (std::abs (firstHalf / 187 - secondHalf / 188) > 0.05f, "the drive changes the sound over the bar");
        }

        beginTest ("Scenes: morph halfway gives the average; Recall-style values stay put");
        {
            Kit kit;
            kit.movementStore().edit ([] (MovementData& d)
            {
                auto g = defaultKitParams().global;
                d.scenes[0].stored = d.scenes[1].stored = true;
                d.scenes[0].values = d.scenes[1].values = g;
                d.scenes[0].values[gp::DistDrive] = 0.0f;
                d.scenes[1].values[gp::DistDrive] = 0.8f;
                d.scenes[0].values[gp::CompDetector] = 0.0f;
                d.scenes[1].values[gp::CompDetector] = 1.0f;
                d.mods[0].targets = {};
            });
            auto params = quietKit();
            params.global[gp::SceneMorphOn] = 1.0f;
            params.global[gp::SceneMorph] = 0.5f;
            kit.setParameters (params);
            kit.prepare (kRate, 256);
            kit.setParameters (params);
            // The effective chain drive is reported through the gain structure; check via settings.
            auto g = params.global;
            g[gp::DistDrive] = 0.4f;
            expectWithinAbsoluteError (kit.getCurrentGlobal (gp::DistDrive), 0.4f, 1.0e-5f);
            expectEquals ((int) kit.getCurrentGlobal (gp::CompDetector), 1, "switches take the second scene from halfway");
            params.global[gp::SceneMorph] = 0.25f;
            kit.setParameters (params);
            expectWithinAbsoluteError (kit.getCurrentGlobal (gp::DistDrive), 0.2f, 1.0e-5f);
            expectEquals ((int) kit.getCurrentGlobal (gp::CompDetector), 0);
        }

        beginTest ("Vary: locked groups never change; results are real alternatives");
        {
            const auto base = defaultKitParams().voices[2]; // snare
            VaryRequest req;
            req.base = base;
            req.amount = 0.4f;
            req.lockEnvelopes = true;
            req.seed = 7;
            const auto result = vary (req);
            logMessage ("  " + juce::String ((int) result.size()) + " candidates");
            expectGreaterOrEqual ((int) result.size(), 3);
            const auto baseF = measureVoice (base, nullptr);
            for (const auto& c : result)
            {
                for (int p = 0; p < kNumVoiceParams; ++p)
                    if (p != vp::Level && (varyGroupOf (p) == VaryGroup::Envelopes || varyGroupOf (p) == VaryGroup::None))
                        expectEquals (c.params[p], base[p], "locked or excluded: " + juce::String (voiceParamSpecs()[(size_t) p].key));
                expect (c.features.peakDb > -45.0f && c.features.peakDb <= 6.0f, "no silent or slamming results");
                const auto differs = std::abs (c.features.loudnessDb - baseF.loudnessDb) > 0.5f
                                  || std::abs (c.features.brightnessHz - baseF.brightnessHz) > 50.0f
                                  || std::abs (c.features.lengthMs - baseF.lengthMs) > 10.0f;
                expect (differs, "not a near-duplicate of the original");
            }
        }

        beginTest ("Vary: directions move the measured sound the right way");
        for (const auto [dir, name] : { std::pair { VaryDirection::Brighter, "brighter" }, std::pair { VaryDirection::Darker, "darker" },
                                        std::pair { VaryDirection::Shorter, "shorter" }, std::pair { VaryDirection::Longer, "longer" } })
        {
            VaryRequest req;
            req.base = defaultKitParams().voices[0]; // kick
            req.amount = 0.4f;
            req.direction = dir;
            req.seed = 11;
            const auto baseF = measureVoice (req.base, nullptr);
            const auto result = vary (req);
            expectGreaterOrEqual ((int) result.size(), 1, name);
            for (const auto& c : result)
            {
                const auto ok = dir == VaryDirection::Brighter ? c.features.brightnessHz > baseF.brightnessHz
                              : dir == VaryDirection::Darker   ? c.features.brightnessHz < baseF.brightnessHz
                              : dir == VaryDirection::Shorter  ? c.features.lengthMs < baseF.lengthMs
                                                               : c.features.lengthMs > baseF.lengthMs;
                expect (ok, name);
            }
        }

        beginTest ("Vary: suggestions are level-matched to the original");
        {
            int count = 0, atCeiling = 0;
            float worst = 0.0f, worstAtCeiling = 0.0f;
            const auto levelMax = voiceParamSpecs()[(size_t) vp::Level].max;
            for (int v = 0; v < kNumVoices; ++v)
                for (const auto dir : { VaryDirection::None, VaryDirection::Shorter, VaryDirection::Longer, VaryDirection::Darker })
                {
                    VaryRequest req;
                    req.base = defaultKitParams().voices[(size_t) v];
                    req.amount = 0.6f;
                    req.direction = dir;
                    req.seed = (uint32_t) (100 + v);
                    const auto baseF = measureVoice (req.base, nullptr);
                    for (const auto& c : vary (req))
                    {
                        // Measured again from scratch, with the matched Level.
                        const auto f = measureVoice (c.params, nullptr);
                        expectWithinAbsoluteError (f.loudnessDb, c.features.loudnessDb, 0.1f, "reported loudness is real");
                        expectWithinAbsoluteError (c.params[vp::Level], req.base[vp::Level] + c.levelDb, 1.0e-4f);
                        const auto miss = std::abs (f.loudnessDb - baseF.loudnessDb);
                        if (c.params[vp::Level] >= levelMax - 1.0e-4f)
                            worstAtCeiling = std::max (worstAtCeiling, miss), ++atCeiling;
                        else
                            worst = std::max (worst, miss);
                        ++count;
                    }
                }
            logMessage ("  " + juce::String (count) + " candidates, worst mismatch " + juce::String (worst, 2) + " dB; "
                        + juce::String (atCeiling) + " at the Level ceiling, worst " + juce::String (worstAtCeiling, 2) + " dB");
            expectGreaterOrEqual (count, 60);
            expectLessOrEqual (worst, 0.1f);
            expectLessOrEqual (worstAtCeiling, 1.5f);
        }

        beginTest ("Vary notes name the two biggest changes you can hear");
        {
            const auto base = defaultKitParams().voices[0]; // the kick: FM
            auto changed = base;
            changed.v[(size_t) vp::AmpD] = base[vp::AmpD] * 0.59f;          // decay −41%
            changed.v[(size_t) vp::Level] = base[vp::Level] + 6.0f;          // the loudness match: never named
            changed.v[(size_t) vp::SmpStart] = 0.4f;                          // a sample setting on an FM sound
            changed.v[(size_t) opParam (3, OpD)] = base.op (3, OpD) * 3.0f;   // a silent operator
            const auto note = juce::String::fromUTF8 (describeChanges (base, changed).c_str());
            expect (note.contains ("decay"), "names the decay: " + note);
            expect (note.contains (juce::String::fromUTF8 ("\xe2\x88\x92") + "41%"), "as a signed percentage: " + note);
            expect (! note.containsIgnoreCase ("level") && ! note.containsIgnoreCase ("start") && ! note.containsIgnoreCase ("op4"),
                    "leaves out Level, the sample and silent operators: " + note);

            auto two = changed;
            two.v[(size_t) opParam (1, OpLevel)] = std::min (1.0f, base.op (1, OpLevel) + 0.3f);
            const auto both = juce::String::fromUTF8 (describeChanges (base, two).c_str());
            expect (both.contains (juce::String::fromUTF8 ("\xc2\xb7")) && both.containsIgnoreCase ("op2 level"), "two changes: " + both);
            expectEquals (juce::String::fromUTF8 (describeChanges (base, base).c_str()), juce::String(), "no changes, no note");
        }

        beginTest ("Host names say sound; IDs stay");
        {
            expectEquals (juce::String (voiceParamName (0, vp::Level)), juce::String ("S1 Level"));
            expectEquals (juce::String (voiceParamID (0, vp::Level)), juce::String ("v1_level"));
            expectEquals (juce::String (globalParamSpecs()[(size_t) gp::KeysVoice].label), juce::String ("Keys Sound"));
            expectEquals (juce::String (globalParamSpecs()[(size_t) gp::KeysVoice].key), juce::String ("keys_voice"));
        }

        beginTest ("Movement data survives XML");
        {
            auto d = defaultMovement();
            setPreset (d.mods[1], ShapePreset::Sine);
            d.mods[1].targets[0] = { true, false, 3, vp::FltCutoff, -0.4f };
            d.scenes[2].stored = true;
            d.scenes[2].values[gp::DistDrive] = 0.66f;
            MovementData back;
            back.fromXml (*d.toXml());
            expectEquals (back.mods[1].numPoints, 4);
            expect (back.mods[1].targets[0] == d.mods[1].targets[0]);
            expectWithinAbsoluteError (back.mods[1].targets[0].depth, -0.4f, 1.0e-4f);
            expect (back.mods[0].targets[0].active == kShipDemoModulation, "Mod 1's demo target only in demo builds");
            expect (back.scenes[2].stored && std::abs (back.scenes[2].values[gp::DistDrive] - 0.66f) < 1.0e-4f);
        }
    }
};

static MovementTests movementTests;

} // namespace batida
