#include "Validate.h"

#include "Engine/Movement/Movement.h"
#include "Engine/Sequencer/Pattern.h"

namespace batida
{

namespace
{
int editDistance (const juce::String& a, const juce::String& b)
{
    std::vector<int> row ((size_t) b.length() + 1);
    for (int j = 0; j <= b.length(); ++j)
        row[(size_t) j] = j;
    for (int i = 1; i <= a.length(); ++i)
    {
        auto diagonal = row[0];
        row[0] = i;
        for (int j = 1; j <= b.length(); ++j)
        {
            const auto above = row[(size_t) j];
            row[(size_t) j] = std::min ({ above + 1, row[(size_t) j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1) });
            diagonal = above;
        }
    }
    return row[(size_t) b.length()];
}

bool isNumber (const juce::String& text)
{
    const auto t = text.trim();
    return t.isNotEmpty() && t.containsOnly ("0123456789.-+eE") && t.containsAnyOf ("0123456789");
}

juce::String unitText (const ParamSpec& spec)
{
    if (spec.unit == "%" || spec.unit == "bi" || spec.unit == "pan" || spec.unit == "wave" || spec.unit == "dist" || spec.unit == "n")
        return {};
    return spec.unit.empty() ? juce::String() : " " + juce::String (spec.unit);
}

juce::String number (float v)
{
    char buffer[32];
    std::snprintf (buffer, sizeof (buffer), "%g", (double) v);
    return buffer;
}

juce::String rangeText (const ParamSpec& spec)
{
    return number (spec.min) + ".." + number (spec.max) + unitText (spec);
}

class Checker
{
public:
    Checker (const juce::File& f, Validation& r) : file (f), result (r)
    {
        for (const auto& s : voiceParamSpecs())
            voiceKeys.add (s.key);
        for (int g = 0; g < kNumGlobalParams; ++g)
            globalKeys.add (globalParamID (g));
    }

    void problem (const juce::String& where, const juce::String& what, bool fatal = false)
    {
        result.problems.push_back ({ where, what, fatal });
    }

    // One setting's text against its spec.
    void value (const juce::String& where, const ParamSpec& spec, const juce::String& text)
    {
        const auto t = text.trim();
        if (spec.kind == Kind::Choice)
        {
            for (const auto& c : spec.choices)
                if (t.equalsIgnoreCase (juce::String (c)))
                    return;
            if (isNumber (t) && t.containsOnly ("0123456789") && t.getIntValue() < (int) spec.choices.size())
                return;
            juce::StringArray names;
            for (const auto& c : spec.choices)
                names.add (c);
            problem (where, "\"" + t + "\" isn't a choice (" + names.joinIntoString (", ") + "); read as \""
                                + juce::String (spec.choices[(size_t) valueFrom (spec, t)]) + "\"");
            return;
        }
        if (spec.kind == Kind::Bool)
        {
            for (auto* ok : { "0", "1", "true", "false", "on", "off", "yes", "no" })
                if (t.equalsIgnoreCase (ok))
                    return;
            problem (where, "\"" + t + "\" isn't a switch value (1/0, true/false, on/off, yes/no); read as "
                                + (valueFrom (spec, t) > 0.5f ? "on" : "off"));
            return;
        }
        if (! isNumber (t))
        {
            problem (where, "\"" + t + "\" isn't a number; read as " + number (valueFrom (spec, t)));
            return;
        }
        const auto v = t.getFloatValue();
        if (v < spec.min || v > spec.max)
            problem (where, t + " is outside " + rangeText (spec) + "; clamped to " + number (valueFrom (spec, t)));
    }

    void unknownKey (const juce::String& where, const juce::String& key, const juce::StringArray& known)
    {
        const auto near = nearestKey (key, known);
        problem (where, "unknown setting, ignored" + (near.isNotEmpty() ? " (did you mean " + near + "?)" : juce::String()));
    }

    void unknownChildren (const juce::XmlElement& e, const juce::String& where, const juce::StringArray& allowed)
    {
        for (auto* c : e.getChildIterator())
            if (! c->isTextElement() && ! allowed.contains (c->getTagName()))
                problem (where + " > " + c->getTagName(), "unknown element, ignored"
                         + (allowed.isEmpty() ? juce::String() : " (expected " + allowed.joinIntoString (", ") + ")"));
    }

    void info (const juce::XmlElement& root)
    {
        const auto* i = root.getChildByName ("INFO");
        if (i == nullptr)
            return;
        for (int a = 0; a < i->getNumAttributes(); ++a)
        {
            const auto name = i->getAttributeName (a);
            if (! juce::StringArray { "name", "author", "category", "tags" }.contains (name))
                unknownKey ("INFO > " + name, name, { "name", "author", "category", "tags" });
        }
        const auto category = i->getStringAttribute ("category").trim();
        if (category.isNotEmpty() && ! soundCategories().contains (category))
            problem ("INFO > category", "\"" + category + "\" isn't a category (" + soundCategories().joinIntoString (", ")
                                            + "); LIB shows it under Other");
    }

    void sample (const juce::XmlElement* e, const juce::String& where)
    {
        if (e == nullptr)
            return;
        for (int a = 0; a < e->getNumAttributes(); ++a)
            if (const auto name = e->getAttributeName (a); ! juce::StringArray { "file", "absolute", "bytes" }.contains (name))
                unknownKey (where + " > SAMPLE > " + name, name, { "file", "absolute", "bytes" });
        const auto relative = e->getStringAttribute ("file"), absolute = e->getStringAttribute ("absolute");
        if (relative.isEmpty() && absolute.isEmpty())
        {
            problem (where + " > SAMPLE", "no file given (file=\"...\", relative to this file, or absolute=\"...\")");
            return;
        }
        const auto dir = file.getParentDirectory();
        if (relative.isNotEmpty() && dir.getChildFile (relative).existsAsFile())
            return;
        if (absolute.isNotEmpty() && juce::File::isAbsolutePath (absolute) && juce::File (absolute).existsAsFile())
            return;
        problem (where + " > SAMPLE", "sample not found: " + (relative.isNotEmpty() ? relative : absolute));
    }

    void voice (const juce::XmlElement& e, const juce::String& where, bool inKit)
    {
        for (int a = 0; a < e.getNumAttributes(); ++a)
        {
            const auto name = e.getAttributeName (a);
            if (inKit && (name == "index" || name == "name"))
                continue;
            const auto k = findVoiceParam (name.toStdString());
            if (k < 0)
                unknownKey (where + " > " + name, name, voiceKeys);
            else
                value (where + " > " + name, voiceParamSpecs()[(size_t) k], e.getAttributeValue (a));
        }
        unknownChildren (e, where, { "SAMPLE" });
        sample (e.getChildByName ("SAMPLE"), where);
    }

    void globals (const juce::XmlElement& e, bool kitFile)
    {
        for (int a = 0; a < e.getNumAttributes(); ++a)
        {
            const auto name = e.getAttributeName (a);
            const auto g = globalKeys.indexOf (name);
            if (g < 0)
                unknownKey ("KIT > " + name, name, globalKeys);
            else if (kitFile && ! isKitGlobal (g))
                problem ("KIT > " + name, "ignored in a kit file (MIDI, sequencer and master settings belong to sets)");
            else
                value ("KIT > " + name, globalParamSpecs()[(size_t) g], e.getAttributeValue (a));
        }
    }

    void kit (const juce::XmlElement* e, bool kitFile)
    {
        if (e == nullptr)
        {
            problem ("KIT", "missing: every sound and kit setting takes its default (the Neutral kit)");
            return;
        }
        globals (*e, kitFile);
        unknownChildren (*e, "KIT", { "VOICE", "MOVEMENT" });
        std::array<bool, kNumVoices> seen {};
        for (auto* v : e->getChildWithTagNameIterator ("VOICE"))
        {
            const auto index = v->getIntAttribute ("index", -1);
            if (! v->hasAttribute ("index") || index < 0 || index >= kNumVoices || ! isNumber (v->getStringAttribute ("index")))
            {
                problem ("KIT > VOICE", "ignored: index must be 0..7 (slot 1 is index 0)");
                continue;
            }
            if (seen[(size_t) index])
                problem ("KIT > VOICE " + juce::String (index), "a second VOICE with this index replaces the first");
            seen[(size_t) index] = true;
            voice (*v, "KIT > VOICE " + juce::String (index), true);
        }
        if (const auto* m = e->getChildByName ("MOVEMENT"))
            movement (*m);
    }

    void movement (const juce::XmlElement& e)
    {
        unknownChildren (e, "MOVEMENT", { "MOD", "SCENE" });
        for (auto* m : e.getChildWithTagNameIterator ("MOD"))
        {
            const auto index = m->getIntAttribute ("index", -1);
            const auto where = "MOVEMENT > MOD " + juce::String (index);
            if (index < 0 || index >= kNumMods)
            {
                problem ("MOVEMENT > MOD", "ignored: index must be 0 or 1");
                continue;
            }
            juce::StringArray points;
            points.addTokens (m->getStringAttribute ("points"), ",", "");
            for (const auto& p : points)
            {
                juce::StringArray v;
                v.addTokens (p, ":", "");
                if (v.size() < 2 || v.size() > 3 || ! isNumber (v[0]) || ! isNumber (v[1]))
                    problem (where + " > points", "\"" + p + "\" isn't x:y or x:y:curve");
            }
            if (points.size() > kMaxModPoints)
                problem (where + " > points", "only the first " + juce::String (kMaxModPoints) + " points are used");
            unknownChildren (*m, where, { "TARGET" });
            for (auto* t : m->getChildWithTagNameIterator ("TARGET"))
            {
                if (t->hasAttribute ("global"))
                {
                    const auto key = t->getStringAttribute ("global");
                    const auto g = globalKeys.indexOf (key);
                    if (g < 0)
                        unknownKey (where + " > TARGET > global", key, globalKeys);
                    else if (! isModulatableGlobal (g))
                        problem (where + " > TARGET > global", key + " can't be modulated; ignored");
                }
                else
                {
                    const auto key = t->getStringAttribute ("param");
                    const auto p = findVoiceParam (key.toStdString());
                    const auto v = t->getIntAttribute ("voice", -1);
                    if (p < 0)
                        unknownKey (where + " > TARGET > param", key, voiceKeys);
                    else if (! isModulatableVoice (p))
                        problem (where + " > TARGET > param", key + " can't be modulated; ignored");
                    if (v < 0 || v >= kNumVoices)
                        problem (where + " > TARGET > voice", "must be 0..7; ignored");
                }
                const auto depth = t->getStringAttribute ("depth", "0.25");
                if (! isNumber (depth) || std::abs (depth.getFloatValue()) > 1.0f)
                    problem (where + " > TARGET > depth", "\"" + depth + "\" must be -1..1");
            }
        }
        for (auto* s : e.getChildWithTagNameIterator ("SCENE"))
        {
            const auto index = s->getIntAttribute ("index", -1);
            if (index < 0 || index >= kNumScenes)
                problem ("MOVEMENT > SCENE", "ignored: index must be 0..3 (A..D)");
        }
    }

    void list (const juce::XmlElement& t, const juce::String& where, const char* name, int lo, int hi)
    {
        if (! t.hasAttribute (name))
            return;
        juce::StringArray items;
        items.addTokens (t.getStringAttribute (name), ",", "");
        for (int s = 0; s < items.size(); ++s)
        {
            const auto& item = items[s];
            if (! isNumber (item) || item.getIntValue() < lo || item.getIntValue() > hi)
            {
                problem (where + " > " + name, "step " + juce::String (s + 1) + ": \"" + item.trim() + "\" must be "
                                                   + juce::String (lo) + ".." + juce::String (hi));
                return; // one per lane is enough
            }
        }
        if (items.size() > kMaxSteps)
            problem (where + " > " + name, juce::String (items.size()) + " values; only the first 64 are used");
    }

    void pattern (const juce::XmlElement& e, const juce::String& where)
    {
        for (int a = 0; a < e.getNumAttributes(); ++a)
            if (const auto name = e.getAttributeName (a); ! juce::StringArray { "length", "xy", "index" }.contains (name))
                unknownKey (where + " > " + name, name, { "length", "xy" });
        const auto length = e.getStringAttribute ("length", "16");
        if (! isNumber (length) || length.getIntValue() < kMinPatternSteps || length.getIntValue() > kMaxSteps)
            problem (where + " > length", "\"" + length + "\" must be 8..64 steps");

        juce::StringArray xy;
        xy.addTokens (e.getStringAttribute ("xy"), ",", "");
        for (int s = 0; s < xy.size(); ++s)
        {
            const auto item = xy[s].trim();
            if (item == "-" || item.isEmpty())
                continue;
            const auto x = item.upToFirstOccurrenceOf (":", false, false), y = item.fromFirstOccurrenceOf (":", false, false);
            if (! item.containsChar (':') || ! isNumber (x) || ! isNumber (y) || x.getFloatValue() < 0.0f
                || x.getFloatValue() > 1.0f || y.getFloatValue() < 0.0f || y.getFloatValue() > 1.0f)
            {
                problem (where + " > xy", "step " + juce::String (s + 1) + ": \"" + item + "\" must be - or x:y (0..1 each)");
                break;
            }
        }

        unknownChildren (e, where, { "TRACK" });
        for (auto* t : e.getChildWithTagNameIterator ("TRACK"))
        {
            const auto v = t->getIntAttribute ("voice", -1);
            if (! t->hasAttribute ("voice") || v < 0 || v >= kNumTracks)
            {
                problem (where + " > TRACK", "ignored: voice must be 0..7 (slot 1 is voice 0)");
                continue;
            }
            const auto tw = where + " > TRACK " + juce::String (v);
            for (int a = 0; a < t->getNumAttributes(); ++a)
                if (const auto name = t->getAttributeName (a);
                    ! juce::StringArray { "voice", "length", "noteLength", "gate", "velocity", "pitch", "slice", "ratchet", "probability" }.contains (name))
                    unknownKey (tw + " > " + name, name, { "length", "noteLength", "gate", "velocity", "pitch", "slice", "ratchet", "probability" });
            if (t->hasAttribute ("length"))
                if (const auto l = t->getStringAttribute ("length"); ! isNumber (l) || l.getIntValue() < 1 || l.getIntValue() > kMaxSteps)
                    problem (tw + " > length", "\"" + l + "\" must be 1..64 steps");
            if (t->hasAttribute ("noteLength"))
                if (const auto l = t->getStringAttribute ("noteLength"); ! isNumber (l) || l.getDoubleValue() < 0.05 || l.getDoubleValue() > 1.0)
                    problem (tw + " > noteLength", "\"" + l + "\" must be 0.05..1 of a step");
            const auto gate = t->getStringAttribute ("gate").removeCharacters (" |");
            if (! gate.containsOnly ("01xX.-"))
                problem (tw + " > gate", "use 1 or x for a hit and 0, . or - for a rest");
            if (gate.length() > kMaxSteps)
                problem (tw + " > gate", juce::String (gate.length()) + " steps; only the first 64 are used");
            list (*t, tw, "velocity", 1, 127);
            list (*t, tw, "pitch", -24, 24);
            list (*t, tw, "slice", 0, kMaxSlices - 1);
            list (*t, tw, "ratchet", 1, kMaxRatchet);
            list (*t, tw, "probability", 0, 100);
        }
    }

    void run()
    {
        if (! file.existsAsFile())
        {
            problem (file.getFileName(), "file not found", true);
            return;
        }
        result.type = presetTypeOf (file);
        if (! result.type)
        {
            problem (file.getFileName(), "not a Batida file: the name must end in .batida-sound, -kit, -pattern or -set", true);
            return;
        }
        juce::XmlDocument doc (file);
        const auto xml = doc.getDocumentElement();
        if (xml == nullptr)
        {
            problem (file.getFileName(), "not readable XML: " + doc.getLastParseError(), true);
            return;
        }
        if (! xml->hasTagName ("BATIDA"))
        {
            problem ("<" + xml->getTagName() + ">", "the root element must be <BATIDA type=\"...\">", true);
            return;
        }
        const auto type = *result.type;
        if (xml->getStringAttribute ("type") != typeName (type))
        {
            problem ("BATIDA > type", "\"" + xml->getStringAttribute ("type") + "\" must be \"" + typeName (type) + "\" for a "
                                          + extensionFor (type) + " file", true);
            return;
        }
        if (xml->getIntAttribute ("format", 1) > kPresetFormat)
            problem ("BATIDA > format", "made by a newer Batida: only what this version knows is read");

        info (*xml);
        switch (type)
        {
            case PresetType::Sound:
                unknownChildren (*xml, "BATIDA", { "INFO", "VOICE" });
                if (const auto* v = xml->getChildByName ("VOICE"))
                    voice (*v, "VOICE", false);
                else
                    problem ("VOICE", "missing: the sound is Init");
                break;
            case PresetType::Kit:
                unknownChildren (*xml, "BATIDA", { "INFO", "KIT" });
                kit (xml->getChildByName ("KIT"), true);
                break;
            case PresetType::Pattern:
                unknownChildren (*xml, "BATIDA", { "INFO", "PATTERN" });
                if (const auto* p = xml->getChildByName ("PATTERN"))
                    pattern (*p, "PATTERN");
                else
                    problem ("PATTERN", "missing: the pattern is empty");
                break;
            case PresetType::Set:
                unknownChildren (*xml, "BATIDA", { "INFO", "KIT", "PATTERNS" });
                kit (xml->getChildByName ("KIT"), false);
                if (const auto* ps = xml->getChildByName ("PATTERNS"))
                {
                    unknownChildren (*ps, "PATTERNS", { "PATTERN" });
                    for (auto* p : ps->getChildWithTagNameIterator ("PATTERN"))
                    {
                        const auto index = p->getIntAttribute ("index", -1);
                        if (index < 0 || index >= kNumPatterns)
                            problem ("PATTERNS > PATTERN", "ignored: index must be 0..15 (pattern 1 is index 0)");
                        else
                            pattern (*p, "PATTERNS > PATTERN " + juce::String (index));
                    }
                }
                break;
        }
    }

private:
    const juce::File& file;
    Validation& result;
    juce::StringArray voiceKeys, globalKeys;
};
} // namespace

bool Validation::loads() const
{
    for (const auto& p : problems)
        if (p.fatal)
            return false;
    return true;
}

juce::String nearestKey (const juce::String& key, const juce::StringArray& known)
{
    juce::String best;
    int bestDistance = std::max (2, key.length() / 3) + 1;
    for (const auto& k : known)
    {
        const auto d = editDistance (key.toLowerCase(), k.toLowerCase());
        if (d < bestDistance)
        {
            bestDistance = d;
            best = k;
        }
    }
    return best;
}

Validation validateFile (const juce::File& file)
{
    Validation result;
    Checker (file, result).run();
    return result;
}

} // namespace batida
