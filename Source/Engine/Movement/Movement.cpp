#include "Movement.h"

#include "Engine/ParamRange.h"

#include <cmath>

namespace batida
{

bool ModSetup::hasTargets() const
{
    for (const auto& t : targets)
        if (t.active)
            return true;
    return false;
}

void setPreset (ModSetup& s, ShapePreset preset)
{
    auto set = [&] (std::initializer_list<ModPoint> pts)
    {
        s.numPoints = 0;
        for (const auto& p : pts)
            s.points[(size_t) s.numPoints++] = p;
    };

    switch (preset)
    {
        // Steep through the middle, flat at the peaks.
        case ShapePreset::Sine:        set ({ { 0.0f, 0.5f, -0.5f }, { 0.25f, 1.0f, 0.5f }, { 0.5f, 0.5f, -0.5f }, { 0.75f, 0.0f, 0.5f } }); break;
        case ShapePreset::Triangle:    set ({ { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f } }); break;
        case ShapePreset::RampUp:      set ({ { 0.0f, 0.0f, 0.0f }, { 0.999f, 1.0f, 0.0f } }); break;
        case ShapePreset::RampDown:    set ({ { 0.0f, 1.0f, 0.0f }, { 0.999f, 0.0f, 0.0f } }); break;
        case ShapePreset::Square:      set ({ { 0.0f, 1.0f, 0.0f }, { 0.499f, 1.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, { 0.999f, 0.0f, 0.0f } }); break;
        case ShapePreset::RandomSteps:
        {
            juce::Random r (0x5eed);
            s.numPoints = 0;
            for (int i = 0; i < 8; ++i)
            {
                const auto y = r.nextFloat();
                s.points[(size_t) s.numPoints++] = { i / 8.0f, y, 0.0f };
                s.points[(size_t) s.numPoints++] = { (i + 1) / 8.0f - 0.001f, y, 0.0f };
            }
            break;
        }
    }
}

float evaluateShape (const ModSetup& s, float phase)
{
    if (s.numPoints <= 0)
        return 0.0f;
    if (s.numPoints == 1)
        return s.points[0].y;

    phase = phase - std::floor (phase);
    const auto n = s.numPoints;

    // Segment i runs from point i to point i+1 (the last wraps to point 0 at x = 1).
    int i = n - 1;
    for (int k = 0; k < n - 1; ++k)
        if (phase < s.points[(size_t) k + 1].x)
        {
            i = k;
            break;
        }

    const auto& a = s.points[(size_t) i];
    const auto bx = i + 1 < n ? s.points[(size_t) i + 1].x : 1.0f + s.points[0].x;
    const auto by = i + 1 < n ? s.points[(size_t) i + 1].y : s.points[0].y;
    const auto span = bx - a.x;
    auto t = span > 1.0e-6f ? std::clamp ((phase - a.x) / span, 0.0f, 1.0f) : 1.0f;

    // Curve: >0 bends the segment late (slow start), <0 early.
    if (a.curve > 0.0f)
        t = std::pow (t, 1.0f + 4.0f * a.curve);
    else if (a.curve < 0.0f)
        t = 1.0f - std::pow (1.0f - t, 1.0f - 4.0f * a.curve);

    return a.y + (by - a.y) * t;
}

const std::vector<int>& sceneParams()
{
    static const std::vector<int> params = []
    {
        std::vector<int> p { gp::XyX, gp::XyY };
        for (int g = gp::CompAmount; g <= gp::SafetyClip; ++g)
            p.push_back (g);
        return p;
    }();
    return params;
}

bool isModulatableGlobal (int g)
{
    if (g == gp::XyX || g == gp::XyY || g == gp::Master || g == gp::SceneMorph)
        return true;
    return g >= gp::CompAmount && g <= gp::ChainOut && isContinuous (globalParamSpecs()[(size_t) g]);
}

bool isModulatableVoice (int p)
{
    return p >= 0 && p < kNumVoiceParams && isContinuous (voiceParamSpecs()[(size_t) p]);
}

MovementData defaultMovement()
{
    MovementData m;
    for (auto& mod : m.mods)
        setPreset (mod, ShapePreset::Triangle);

    if (kShipDemoModulation)
    {
        // Mod 1: a gentle rise and fall of Heat over one bar.
        auto& t = m.mods[0].targets[0];
        t.active = true;
        t.global = true;
        t.param = gp::XyY;
        t.depth = 0.25f;
    }
    return m;
}

// XML --------------------------------------------------------------------------

std::unique_ptr<juce::XmlElement> MovementData::toXml() const
{
    auto root = std::make_unique<juce::XmlElement> ("MOVEMENT");

    for (int m = 0; m < kNumMods; ++m)
    {
        const auto& mod = mods[(size_t) m];
        auto* me = root->createNewChildElement ("MOD");
        me->setAttribute ("index", m);

        juce::StringArray pts;
        for (int i = 0; i < mod.numPoints; ++i)
        {
            const auto& p = mod.points[(size_t) i];
            pts.add (juce::String (p.x, 4) + ":" + juce::String (p.y, 4) + ":" + juce::String (p.curve, 3));
        }
        me->setAttribute ("points", pts.joinIntoString (","));

        for (const auto& t : mod.targets)
        {
            if (! t.active)
                continue;
            auto* te = me->createNewChildElement ("TARGET");
            // Stored by parameter key, so the layout of the parameter table can change.
            if (t.global)
                te->setAttribute ("global", globalParamID (t.param));
            else
            {
                te->setAttribute ("voice", t.voice);
                te->setAttribute ("param", voiceParamSpecs()[(size_t) t.param].key);
            }
            te->setAttribute ("depth", t.depth);
        }
    }

    for (int s = 0; s < kNumScenes; ++s)
    {
        const auto& sc = scenes[(size_t) s];
        if (! sc.stored)
            continue;
        auto* se = root->createNewChildElement ("SCENE");
        se->setAttribute ("index", s);
        for (const auto g : sceneParams())
            se->setAttribute (juce::String (globalParamID (g)), sc.values[(size_t) g]);
    }
    return root;
}

void MovementData::fromXml (const juce::XmlElement& xml)
{
    *this = MovementData {};
    for (auto& mod : mods)
        setPreset (mod, ShapePreset::Triangle);

    for (auto* me : xml.getChildWithTagNameIterator ("MOD"))
    {
        const auto m = me->getIntAttribute ("index", -1);
        if (m < 0 || m >= kNumMods)
            continue;
        auto& mod = mods[(size_t) m];

        juce::StringArray pts;
        pts.addTokens (me->getStringAttribute ("points"), ",", "");
        if (! pts.isEmpty())
            mod.numPoints = 0;
        for (const auto& p : pts)
        {
            juce::StringArray v;
            v.addTokens (p, ":", "");
            if (v.size() >= 2 && mod.numPoints < kMaxModPoints)
                mod.points[(size_t) mod.numPoints++] = { juce::jlimit (0.0f, 1.0f, v[0].getFloatValue()),
                                                         juce::jlimit (0.0f, 1.0f, v[1].getFloatValue()),
                                                         v.size() > 2 ? juce::jlimit (-1.0f, 1.0f, v[2].getFloatValue()) : 0.0f };
        }

        int n = 0;
        for (auto* te : me->getChildWithTagNameIterator ("TARGET"))
        {
            if (n >= kMaxModTargets)
                break;
            ModTarget t;
            t.depth = (float) juce::jlimit (-1.0, 1.0, te->getDoubleAttribute ("depth", 0.25));
            if (te->hasAttribute ("global"))
            {
                const auto id = te->getStringAttribute ("global").toStdString();
                for (int g = 0; g < kNumGlobalParams; ++g)
                    if (globalParamID (g) == id && isModulatableGlobal (g))
                        t = { true, true, 0, g, t.depth };
            }
            else
            {
                const auto p = findVoiceParam (te->getStringAttribute ("param").toStdString());
                const auto v = te->getIntAttribute ("voice", -1);
                if (p >= 0 && v >= 0 && v < kNumVoices && isModulatableVoice (p))
                    t = { true, false, v, p, t.depth };
            }
            if (t.active)
                mod.targets[(size_t) n++] = t;
        }
    }

    for (auto* se : xml.getChildWithTagNameIterator ("SCENE"))
    {
        const auto s = se->getIntAttribute ("index", -1);
        if (s < 0 || s >= kNumScenes)
            continue;
        auto& sc = scenes[(size_t) s];
        sc.stored = true;
        // Kept inside each setting's range: a morph hands these straight to the chain.
        for (const auto g : sceneParams())
        {
            const auto& spec = globalParamSpecs()[(size_t) g];
            const auto v = (float) se->getDoubleAttribute (juce::String (globalParamID (g)), spec.def);
            sc.values[(size_t) g] = std::isfinite (v) ? juce::jlimit (spec.min, spec.max, v) : spec.def;
        }
    }
}

} // namespace batida
