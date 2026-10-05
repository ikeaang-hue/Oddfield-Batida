#include "PresetFiles.h"

#include <cmath>
#include <cstdio>

#ifndef BATIDA_VERSION
 #define BATIDA_VERSION "dev"
#endif

namespace batida
{

namespace
{
const juce::Identifier kRoot ("BATIDA");

juce::String number (float v)
{
    // 9 significant digits: every float reads back exactly.
    char buffer[32];
    std::snprintf (buffer, sizeof (buffer), "%.9g", (double) v);
    return buffer;
}

juce::String valueText (const ParamSpec& spec, float v)
{
    if (spec.kind == Kind::Choice)
    {
        const auto i = std::clamp ((int) (v + 0.5f), 0, (int) spec.choices.size() - 1);
        return spec.choices[(size_t) i];
    }
    if (spec.kind == Kind::Bool)
        return v > 0.5f ? "1" : "0";
    return number (v);
}

} // namespace

// Choices by name in any case (a menu can gain entries later); numbers as a
// fallback. Switches take 1/0, true/false, on/off or yes/no.
float valueFrom (const ParamSpec& spec, const juce::String& raw)
{
    const auto text = raw.trim();
    if (spec.kind == Kind::Choice)
    {
        for (size_t i = 0; i < spec.choices.size(); ++i)
            if (text.equalsIgnoreCase (juce::String (spec.choices[i])))
                return (float) i;
        return (float) juce::jlimit (0, (int) spec.choices.size() - 1, text.getIntValue());
    }
    if (spec.kind == Kind::Bool)
    {
        for (auto* word : { "true", "on", "yes" })
            if (text.equalsIgnoreCase (word))
                return 1.0f;
        return text.getIntValue() != 0 ? 1.0f : 0.0f;
    }
    const auto v = text.getFloatValue();
    return std::isnan (v) ? spec.def : juce::jlimit (spec.min, spec.max, v); // "nan" would pass any limit
}

namespace
{

void writeVoice (juce::XmlElement& e, const VoiceParams& p)
{
    for (int k = 0; k < kNumVoiceParams; ++k)
    {
        const auto& spec = voiceParamSpecs()[(size_t) k];
        e.setAttribute (juce::String (spec.key), valueText (spec, p[k]));
    }
}

VoiceParams readVoice (const juce::XmlElement& e)
{
    VoiceParams p;
    for (int k = 0; k < kNumVoiceParams; ++k)
    {
        const auto& spec = voiceParamSpecs()[(size_t) k];
        const juce::String key (spec.key);
        p[k] = e.hasAttribute (key) ? valueFrom (spec, e.getStringAttribute (key)) : spec.def;
    }
    return p;
}

void writeGlobals (juce::XmlElement& e, const std::array<float, kNumGlobalParams>& g, bool kitOnly)
{
    for (int k = 0; k < kNumGlobalParams; ++k)
        if (! kitOnly || isKitGlobal (k))
            e.setAttribute (juce::String (globalParamID (k)), valueText (globalParamSpecs()[(size_t) k], g[(size_t) k]));
}

std::array<float, kNumGlobalParams> readGlobals (const juce::XmlElement* e)
{
    std::array<float, kNumGlobalParams> g {};
    for (int k = 0; k < kNumGlobalParams; ++k)
    {
        const auto& spec = globalParamSpecs()[(size_t) k];
        const juce::String key (globalParamID (k));
        g[(size_t) k] = e != nullptr && e->hasAttribute (key) ? valueFrom (spec, e->getStringAttribute (key)) : spec.def;
    }
    return g;
}

void writeSample (juce::XmlElement& parent, const SampleRef& s, const juce::File& file)
{
    if (s.isEmpty())
        return;
    const juce::File sample (s.path);
    auto* e = parent.createNewChildElement ("SAMPLE");
    e->setAttribute ("file", sample.getRelativePathFrom (file.getParentDirectory()));
    e->setAttribute ("absolute", s.path);
    const auto bytes = sample.existsAsFile() ? sample.getSize() : s.bytes;
    if (bytes >= 0)
        e->setAttribute ("bytes", juce::String (bytes));
}

SampleRef readSample (const juce::XmlElement* e, const juce::File& file)
{
    SampleRef s;
    if (e == nullptr)
        return s;
    s.bytes = e->getStringAttribute ("bytes", "-1").getLargeIntValue();
    const auto relative = e->getStringAttribute ("file");
    const auto absolute = e->getStringAttribute ("absolute");
    if (relative.isNotEmpty())
        if (const auto f = file.getParentDirectory().getChildFile (relative); f.existsAsFile())
            return { f.getFullPathName(), s.bytes };
    if (absolute.isNotEmpty() && juce::File::isAbsolutePath (absolute))
        s.path = absolute;
    else if (relative.isNotEmpty())
        s.path = file.getParentDirectory().getChildFile (relative).getFullPathName();
    return s;
}

std::unique_ptr<juce::XmlElement> makeRoot (PresetType type, const PresetInfo& info)
{
    auto root = std::make_unique<juce::XmlElement> (kRoot);
    root->setAttribute ("type", typeName (type));
    root->setAttribute ("format", kPresetFormat);
    root->setAttribute ("madeWith", BATIDA_VERSION);
    auto* i = root->createNewChildElement ("INFO");
    i->setAttribute ("name", info.name);
    i->setAttribute ("author", info.author);
    i->setAttribute ("category", info.category);
    i->setAttribute ("tags", info.tags.joinIntoString (","));
    return root;
}

bool save (const juce::File& file, const juce::XmlElement& xml, juce::String* error)
{
    if (! file.getParentDirectory().createDirectory())
    {
        if (error != nullptr)
            *error = "Can't create the folder " + file.getParentDirectory().getFullPathName();
        return false;
    }
    // Write next to the file, then move it into place, so a failed save never
    // leaves half a file behind.
    const juce::TemporaryFile temp (file);
    if (! xml.writeTo (temp.getFile(), presetTextFormat()) || ! temp.overwriteTargetFileWithTemporary())
    {
        if (error != nullptr)
            *error = "Can't write " + file.getFullPathName();
        return false;
    }
    return true;
}

std::unique_ptr<juce::XmlElement> load (const juce::File& file, PresetType type, PresetInfo& info, juce::String* error)
{
    auto fail = [&] (const juce::String& why) -> std::unique_ptr<juce::XmlElement>
    {
        if (error != nullptr)
            *error = why;
        return nullptr;
    };
    if (! file.existsAsFile())
        return fail ("Can't find " + file.getFullPathName());
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (kRoot))
        return fail (file.getFileName() + " isn't a Batida file");
    if (xml->getStringAttribute ("type") != typeName (type))
        return fail (file.getFileName() + " is a " + xml->getStringAttribute ("type") + ", not a " + typeName (type));

    info = {};
    info.format = xml->getIntAttribute ("format", 1);
    info.madeWith = xml->getStringAttribute ("madeWith");
    if (const auto* i = xml->getChildByName ("INFO"))
    {
        info.name = i->getStringAttribute ("name");
        info.author = i->getStringAttribute ("author");
        info.category = i->getStringAttribute ("category");
        info.tags.addTokens (i->getStringAttribute ("tags"), ",", "");
        info.tags.trim();
        info.tags.removeEmptyStrings();
        // Files saved before the tags were renamed (0.9.1 and earlier).
        static const std::pair<const char*, const char*> renamed[] {
            { "warm", "round" }, { "harsh", "bright" }, { "digital", "synthetic" }, { "metallic", "inharmonic" },
            { "organic", "natural" }, { "neutral", "clean" }, { "synth-aggressive", "driven" }, { "neon", "glossy" },
            { "tender", "soft" } };
        for (auto& t : info.tags)
            for (const auto& [from, to] : renamed)
                if (t == from)
                    t = to;
        info.tags.removeDuplicates (false);
    }
    if (info.name.isEmpty())
        info.name = file.getFileNameWithoutExtension();
    return xml;
}

std::unique_ptr<juce::XmlElement> kitXml (const KitPreset& kit, const juce::File& file, bool allGlobals)
{
    auto e = std::make_unique<juce::XmlElement> ("KIT");
    writeGlobals (*e, kit.globals, ! allGlobals);
    for (int v = 0; v < kNumVoices; ++v)
    {
        auto* ve = e->createNewChildElement ("VOICE");
        ve->setAttribute ("index", v);
        ve->setAttribute ("name", kit.names[(size_t) v]);
        writeVoice (*ve, kit.voices[(size_t) v]);
        writeSample (*ve, kit.samples[(size_t) v], file);
    }
    e->addChildElement (kit.movement.toXml().release());
    return e;
}

void readKitXml (const juce::XmlElement* e, const juce::File& file, KitPreset& kit)
{
    const auto defaults = defaultKitPreset();
    kit.voices = defaults.voices;
    kit.names = defaults.names;
    kit.globals = readGlobals (e);
    kit.movement = MovementData {};
    for (auto& mod : kit.movement.mods)
        setPreset (mod, ShapePreset::Triangle);
    if (e == nullptr)
        return;

    for (auto* ve : e->getChildWithTagNameIterator ("VOICE"))
    {
        const auto v = ve->getIntAttribute ("index", -1);
        if (v < 0 || v >= kNumVoices)
            continue;
        kit.voices[(size_t) v] = readVoice (*ve);
        kit.names[(size_t) v] = ve->getStringAttribute ("name", kit.names[(size_t) v]);
        kit.samples[(size_t) v] = readSample (ve->getChildByName ("SAMPLE"), file);
    }
    if (const auto* m = e->getChildByName ("MOVEMENT"))
        kit.movement.fromXml (*m);
}
} // namespace

juce::String fileComment()
{
    return juce::String ("<!-- Batida ") + BATIDA_VERSION + ". Every setting, its range and unit: " + kReferenceUrl
           + " (or run: batida params). -->";
}

juce::XmlElement::TextFormat presetTextFormat()
{
    juce::XmlElement::TextFormat format;
    format.customHeader = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n" + fileComment();
    return format;
}

const char* extensionFor (PresetType type)
{
    switch (type)
    {
        case PresetType::Sound:   return ".batida-sound";
        case PresetType::Kit:     return ".batida-kit";
        case PresetType::Pattern: return ".batida-pattern";
        case PresetType::Set:     return ".batida-set";
    }
    return "";
}

const char* typeName (PresetType type)
{
    switch (type)
    {
        case PresetType::Sound:   return "sound";
        case PresetType::Kit:     return "kit";
        case PresetType::Pattern: return "pattern";
        case PresetType::Set:     return "set";
    }
    return "";
}

std::optional<PresetType> presetTypeOf (const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();
    for (auto t : { PresetType::Sound, PresetType::Kit, PresetType::Pattern, PresetType::Set })
        if (ext == extensionFor (t))
            return t;
    return std::nullopt;
}

const juce::StringArray& soundCategories()
{
    static const juce::StringArray c { "kick", "snare", "hat", "perc", "fx", "bass", "texture" };
    return c;
}

const juce::StringArray& characterTags()
{
    static const juce::StringArray t { "round", "bright", "synthetic", "inharmonic", "natural" };
    return t;
}

juce::String guessCategory (const juce::String& soundName)
{
    const auto n = soundName.toLowerCase();
    auto has = [&] (std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (n.contains (w))
                return true;
        return false;
    };
    if (has ({ "kick", "bass drum", "boom" }))       return "kick";
    if (has ({ "snare", "clap", "snap" }))           return "snare";
    if (has ({ "hat", "hh", "cymbal", "ride", "crash", "shaker" })) return "hat";
    if (has ({ "bass", "sub", "808", "reese" }))     return "bass";
    if (has ({ "fx", "riser", "sweep", "noise", "zap", "impact" })) return "fx";
    if (has ({ "pad", "texture", "drone", "atmos" })) return "texture";
    if (has ({ "tom", "rim", "perc", "conga", "bongo", "cowbell", "clave", "block", "tick" })) return "perc";
    return {};
}

bool isKitGlobal (int g)
{
    return (g >= gp::XyX && g <= gp::SafetyClip) || (g >= gp::ModBase && g < gp::Count);
}

bool writeSound (const juce::File& file, const SoundPreset& preset, juce::String* error)
{
    auto root = makeRoot (PresetType::Sound, preset.info);
    auto* v = root->createNewChildElement ("VOICE");
    writeVoice (*v, preset.params);
    writeSample (*v, preset.sample, file);
    return save (file, *root, error);
}

bool writeKit (const juce::File& file, const KitPreset& preset, juce::String* error)
{
    auto root = makeRoot (PresetType::Kit, preset.info);
    root->addChildElement (kitXml (preset, file, false).release());
    return save (file, *root, error);
}

bool writePattern (const juce::File& file, const PatternPreset& preset, juce::String* error)
{
    auto root = makeRoot (PresetType::Pattern, preset.info);
    root->addChildElement (patternToXml (preset.pattern).release());
    return save (file, *root, error);
}

bool writeSet (const juce::File& file, const SetPreset& preset, juce::String* error)
{
    auto root = makeRoot (PresetType::Set, preset.info);
    root->addChildElement (kitXml (preset.kit, file, true).release());
    root->addChildElement (preset.patterns.toXml().release());
    return save (file, *root, error);
}

std::optional<SoundPreset> readSound (const juce::File& file, juce::String* error)
{
    SoundPreset s;
    const auto xml = load (file, PresetType::Sound, s.info, error);
    if (xml == nullptr)
        return std::nullopt;
    const auto* v = xml->getChildByName ("VOICE");
    s.params = v != nullptr ? readVoice (*v) : initSoundPreset().params;
    s.sample = readSample (v != nullptr ? v->getChildByName ("SAMPLE") : nullptr, file);
    return s;
}

std::optional<KitPreset> readKit (const juce::File& file, juce::String* error)
{
    KitPreset k;
    const auto xml = load (file, PresetType::Kit, k.info, error);
    if (xml == nullptr)
        return std::nullopt;
    readKitXml (xml->getChildByName ("KIT"), file, k);
    return k;
}

std::optional<PatternPreset> readPattern (const juce::File& file, juce::String* error)
{
    PatternPreset p;
    const auto xml = load (file, PresetType::Pattern, p.info, error);
    if (xml == nullptr)
        return std::nullopt;
    if (const auto* pe = xml->getChildByName ("PATTERN"))
        patternFromXml (*pe, p.pattern);
    return p;
}

std::optional<SetPreset> readSet (const juce::File& file, juce::String* error)
{
    SetPreset s;
    const auto xml = load (file, PresetType::Set, s.info, error);
    if (xml == nullptr)
        return std::nullopt;
    readKitXml (xml->getChildByName ("KIT"), file, s.kit);
    s.kit.info = s.info;
    if (const auto* pe = xml->getChildByName ("PATTERNS"))
        s.patterns.fromXml (*pe);
    return s;
}

std::optional<PresetInfo> readInfo (const juce::File& file)
{
    const auto type = presetTypeOf (file);
    if (! type)
        return std::nullopt;
    PresetInfo info;
    if (load (file, *type, info, nullptr) == nullptr)
        return std::nullopt;
    return info;
}

juce::String safeFileName (const juce::String& name)
{
    auto s = name.trim().replaceCharacters ("/\\:", "---").removeCharacters ("?*\"<>|");
    s = juce::File::createLegalFileName (s).trim();
    while (s.startsWithChar ('.'))
        s = s.substring (1);
    return s.isEmpty() ? juce::String ("Untitled") : s.substring (0, 80);
}

KitPreset defaultKitPreset()
{
    KitPreset k;
    k.info.name = "Neutral";
    k.info.author = "Oddfield";
    const auto params = defaultKitParams();
    k.voices = params.voices;
    k.globals = params.global;
    for (int v = 0; v < kNumVoices; ++v)
        k.names[(size_t) v] = defaultVoiceName (v);
    k.movement = defaultMovement();
    return k;
}

SoundPreset initSoundPreset()
{
    SoundPreset s;
    s.info.name = "Init";
    for (int k = 0; k < kNumVoiceParams; ++k)
        s.params[k] = voiceParamSpecs()[(size_t) k].def;
    return s;
}

} // namespace batida
