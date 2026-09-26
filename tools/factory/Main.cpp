#include "Factory.h"

#include "Library/Library.h"

#include <cmath>
#include <map>
#include <set>

// BatidaFactory: the preset factory.
//
//   BatidaFactory candidates [--library <root>]
//       Makes every sound candidate and writes the ones that pass into
//       <library>/Review for listening in the plugin (dev builds). Sounds you
//       kept stay as they are; everything else is made again.
//   BatidaFactory build [--library <root>] [--out <folder>]
//       Takes the kept sounds (and the best unreviewed ones where a recipe
//       has too few), assembles the kits, writes the patterns and sets, and
//       writes the factory to resources/factory (and the kits, patterns and
//       sets to Review too).
//   BatidaFactory calibrate
//       Measures the neutral kit's sounds (the loudness reference).

using namespace batida;
using namespace batida::factory;

namespace
{
const juce::String kAuthor = "Negative Space";

juce::File sourceRoot()
{
    // tools/factory/Main.cpp → the repository.
    return juce::File (__FILE__).getParentDirectory().getParentDirectory().getParentDirectory();
}

juce::String argument (int argc, char** argv, const char* name, const juce::String& fallback)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (juce::String (argv[i]) == name)
            return argv[i + 1];
    return fallback;
}

juce::String idOf (const Candidate& c) { return juce::String (c.archetype) + "#" + juce::String (c.index); }

juce::String soundPath (const juce::String& category, const juce::String& name)
{
    return "Sounds/" + Library::categoryFolder (category) + "/" + safeFileName (name) + ".batida-sound";
}

juce::String samplePath (const juce::String& category, const juce::String& name)
{
    return "Samples/" + Library::categoryFolder (category) + "/" + safeFileName (name) + ".flac";
}

// Review state ---------------------------------------------------------------------

struct ReviewState
{
    std::map<juce::String, juce::String> decisions; // relative path → keep | reject
    std::map<juce::String, juce::String> pathOfId;   // candidate id → relative path
    std::map<juce::String, juce::String> idOfPath;
};

ReviewState readReview (const juce::File& review)
{
    ReviewState s;
    if (const auto d = juce::JSON::parse (review.getChildFile ("decisions.json")); d.isObject())
        for (const auto& p : d.getDynamicObject()->getProperties())
            s.decisions[p.name.toString()] = p.value.toString();
    if (const auto m = juce::JSON::parse (review.getChildFile ("candidates.json")); m.isArray())
        for (const auto& e : *m.getArray())
        {
            const auto path = e["path"].toString(), id = e["id"].toString();
            s.pathOfId[id] = path;
            s.idOfPath[path] = id;
        }
    return s;
}

juce::String decisionFor (const ReviewState& s, const juce::String& id)
{
    if (const auto p = s.pathOfId.find (id); p != s.pathOfId.end())
        if (const auto d = s.decisions.find (p->second); d != s.decisions.end())
            return d->second;
    return {};
}

bool writeCandidate (const juce::File& root, const Candidate& c, juce::String& relative)
{
    const auto& a = *findArchetype (c.archetype);
    SoundPreset s;
    s.info.name = c.name;
    s.info.author = kAuthor;
    s.info.category = a.category;
    s.info.tags = c.tags;
    s.params = c.params;
    relative = soundPath (a.category, c.name);
    if (c.sample != nullptr)
    {
        const auto f = root.getChildFile (samplePath (a.category, c.name));
        if (! writeFlac (f, c.sampleAudio))
            return false;
        s.sample = { f.getFullPathName(), f.getSize() };
    }
    const auto file = root.getChildFile (relative);
    file.getParentDirectory().createDirectory();
    return writeSound (file, s);
}

std::vector<Candidate> makeNamed (const ReviewState& state)
{
    auto all = makeCandidates();
    std::vector<Candidate*> named;
    for (auto& c : all)
        if (c.dropped.isEmpty())
            named.push_back (&c);
    std::map<juce::String, juce::String> fixed; // names from the last round keep their candidates
    for (const auto& [id, path] : state.pathOfId)
        fixed[id] = path.fromLastOccurrenceOf ("/", false, false).upToLastOccurrenceOf (".batida-sound", false, false);
    nameCandidates (named, fixed);
    return all;
}

int runCandidates (const juce::File& library)
{
    const auto review = library.getChildFile ("Review");
    auto state = readReview (review);

    // What was kept stays, with its name, as the listener left it.
    std::set<juce::String> keptIds, keptPaths;
    for (const auto& [path, decision] : state.decisions)
        if (decision == "keep")
        {
            keptPaths.insert (path);
            if (const auto id = state.idOfPath.find (path); id != state.idOfPath.end())
                keptIds.insert (id->second);
        }

    // Everything else in Review/Sounds and Review/Samples is made again.
    for (const auto& folder : { "Sounds", "Samples" })
        for (const auto& entry : juce::RangedDirectoryIterator (review.getChildFile (folder), true, "*", juce::File::findFiles))
        {
            const auto rel = entry.getFile().getRelativePathFrom (review).replaceCharacter ('\\', '/');
            bool keep = keptPaths.count (rel) > 0;
            for (const auto& k : keptPaths) // a kept sound's sample stays too
                if (rel.startsWith ("Samples/") && k.fromFirstOccurrenceOf ("Sounds/", false, false).upToLastOccurrenceOf (".", false, false)
                                                    == rel.fromFirstOccurrenceOf ("Samples/", false, false).upToLastOccurrenceOf (".", false, false))
                    keep = true;
            if (! keep)
                entry.getFile().deleteFile();
        }

    auto all = makeNamed (state);
    juce::Array<juce::var> manifest;
    std::map<std::string, std::pair<int, int>> counts; // archetype → made, dropped
    std::map<juce::String, int> reasons;
    int written = 0;
    for (auto& c : all)
    {
        auto& n = counts[c.archetype];
        ++n.first;
        if (c.dropped.isNotEmpty())
        {
            ++n.second;
            ++reasons[c.dropped];
            continue;
        }
        const auto id = idOf (c);
        juce::String rel;
        if (keptIds.count (id))
            rel = state.pathOfId[id];
        else
        {
            if (keptPaths.count (soundPath (findArchetype (c.archetype)->category, c.name)))
                c.name += " 2"; // a kept sound has this name
            if (! writeCandidate (review, c, rel))
            {
                std::fprintf (stderr, "could not write %s\n", c.name.toRawUTF8());
                return 1;
            }
        }
        ++written;
        auto* o = new juce::DynamicObject();
        o->setProperty ("path", rel);
        o->setProperty ("id", id);
        o->setProperty ("archetype", juce::String (c.archetype));
        o->setProperty ("seed", (juce::int64) c.seed);
        o->setProperty ("score", c.score);
        manifest.add (juce::var (o));
    }
    review.getChildFile ("candidates.json").replaceWithText (juce::JSON::toString (juce::var (manifest)));

    // Decisions about sounds that are gone are dropped; kits and the rest stay.
    auto* d = new juce::DynamicObject();
    for (const auto& [path, decision] : state.decisions)
        if (decision == "keep" || ! path.startsWith ("Sounds/"))
            d->setProperty (path, decision);
    review.getChildFile ("decisions.json").replaceWithText (juce::JSON::toString (juce::var (d)));

    std::printf ("%-18s %5s %5s %5s\n", "recipe", "made", "duds", "keep");
    for (const auto& a : archetypes())
    {
        const auto& n = counts[a.id];
        std::printf ("%-18s %5d %5d %5d\n", a.id.c_str(), n.first, n.second, a.keep);
    }
    for (const auto& [why, count] : reasons)
        std::printf ("  dropped, %s: %d\n", why.toRawUTF8(), count);
    std::printf ("%d candidates in %s\n", written, review.getFullPathName().toRawUTF8());
    return 0;
}

// Build ------------------------------------------------------------------------------

struct PoolSound
{
    std::string archetype;
    SoundPreset preset;
    juce::AudioBuffer<float> audio; // the sample, if any
    std::shared_ptr<SampleSlot> slot;
    Features features;
    float score = 0.0f;
    bool kept = false;
    int uses = 0;
};

juce::String sampleRelFor (const PoolSound& s) { return samplePath (s.preset.info.category, s.preset.info.name); }

const PoolSound* pick (std::vector<PoolSound>& pool, const SlotSpec& slot, const std::set<const PoolSound*>& inKit)
{
    for (const auto& arch : slot.archetypes)
    {
        PoolSound* best = nullptr;
        float bestScore = -1.0e9f;
        for (auto& s : pool)
        {
            if (s.archetype != arch || inKit.count (&s))
                continue;
            float score = s.score * 0.1f - (float) s.uses * 3.0f + (s.kept ? 2.0f : 0.0f);
            for (const auto& t : slot.prefer)
                if (s.preset.info.tags.contains (juce::String (t)))
                    score += 2.0f;
            if (score > bestScore)
            {
                bestScore = score;
                best = &s;
            }
        }
        if (best != nullptr)
        {
            ++best->uses;
            return best;
        }
    }
    return nullptr;
}

void storeScenes (KitPreset& kit, const KitConcept& idea)
{
    auto& g = kit.globals;
    auto at = [&] (std::array<float, kNumGlobalParams> values, const Overrides& o)
    {
        applyGlobals (values, o);
        Scene s;
        s.stored = true;
        for (const auto p : sceneParams())
            s.values[(size_t) p] = values[(size_t) p];
        return s;
    };
    const auto x = g[gp::XyX], y = g[gp::XyY];
    kit.movement.scenes[0] = at (g, {});
    kit.movement.scenes[1] = at (g, idea.scenes[1].empty()
                                        ? Overrides { { "xy_y", std::min (1.0f, y + 0.3f) },
                                                      { "comp_amount", std::min (1.0f, g[gp::CompAmount] + 0.15f) } }
                                        : idea.scenes[1]);
    kit.movement.scenes[2] = at (g, idea.scenes[2].empty()
                                        ? Overrides { { "xy_x", x < 0.5f ? x + 0.45f : x - 0.45f },
                                                      { "exc_amount", std::min (1.0f, g[gp::ExcAmount] + 0.15f) } }
                                        : idea.scenes[2]);
    kit.movement.scenes[3] = at (g, idea.scenes[3].empty()
                                        ? Overrides { { "eq_hp", 220.0f }, { "eq_lp", 3500.0f }, { "xy_y", std::max (0.0f, y - 0.15f) } }
                                        : idea.scenes[3]);
}

struct BuiltKit
{
    KitPreset kit;
    std::array<std::shared_ptr<SampleSlot>, kNumVoices> slots;
    std::array<juce::String, kNumVoices> sampleRel; // where each slot's sample lives, relative to the library tree
    Pattern pattern;
    const KitConcept* idea = nullptr;
};

constexpr float kPeakLimitDb = -0.5f;

void setKitLevel (BuiltKit& b, float targetDb, bool report)
{
    auto measure = [&] (const Overrides& extra)
    {
        const auto audio = renderKit (b.kit, b.slots, b.pattern, b.idea->tempo, b.idea->swing, 4, extra);
        return std::pair { beatLoudnessDb (audio), peakDb (audio) };
    };
    // Chain Out matches the loudness, but no hit peaks above -0.5 dBFS (the
    // safety clip stays out of it). Three passes, as the chain reacts.
    auto level = [&]
    {
        for (int pass = 0; pass < 3; ++pass)
        {
            const auto [loud, peak] = measure ({});
            const auto change = std::min (targetDb - loud, kPeakLimitDb - peak);
            b.kit.globals[gp::ChainOut] = std::clamp (b.kit.globals[gp::ChainOut] + change, -24.0f, 12.0f);
        }
        return measure ({}).first;
    };
    if (! b.idea->neutral)
    {
        // A kit its peaks hold back gets glue: more compression (up to +0.25)
        // with a faster attack, kept only where it makes the kit louder and
        // full heat stays within 4 dB of it.
        auto loud = level();
        const auto start = b.kit.globals;
        auto best = start;
        auto bestLoud = loud;
        for (float extra = 0.05f; bestLoud < targetDb - 1.0f && extra <= 0.251f; extra += 0.05f)
        {
            b.kit.globals = start;
            b.kit.globals[gp::CompAmount] = std::min (1.0f, start[gp::CompAmount] + extra);
            b.kit.globals[gp::CompAttack] = std::min (start[gp::CompAttack], 2.0f);
            const auto l = level();
            const auto hot = std::min (measure ({ { "xy_x", 0.0f }, { "xy_y", 1.0f } }).first,
                                       measure ({ { "xy_x", 1.0f }, { "xy_y", 1.0f } }).first);
            if (l > bestLoud + 0.3f && hot > l - 4.0f) // and full heat stays about as loud
            {
                bestLoud = l;
                best = b.kit.globals;
            }
        }
        b.kit.globals = best;
    }
    if (! report)
        return;
    const auto [loud, peak] = measure ({});
    std::printf ("%-12s %6.1f %6.1f  out %+5.1f |", b.kit.info.name.toRawUTF8(), loud, peak, b.kit.globals[gp::ChainOut]);
    for (const auto& [x, y] : { std::pair { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f } })
    {
        const auto [l, p] = measure ({ { "xy_x", x }, { "xy_y", y } });
        std::printf (" %+5.1f/%5.1f", l - loud, p);
    }
    std::printf ("\n");
}

int runBuild (const juce::File& library, const juce::File& out)
{
    const auto review = library.getChildFile ("Review");
    const auto state = readReview (review);
    auto all = makeNamed (state);

    // The pool: what was kept, then the best of the rest where a recipe is short.
    std::vector<PoolSound> pool;
    pool.reserve (all.size() + 8);
    int kept = 0, rejected = 0;
    for (const auto& a : archetypes())
    {
        std::vector<Candidate*> keep, rest;
        for (auto& c : all)
        {
            if (c.archetype != a.id || c.dropped.isNotEmpty())
                continue;
            const auto d = decisionFor (state, idOf (c));
            if (d == "keep") keep.push_back (&c);
            else if (d == "reject") ++rejected;
            else rest.push_back (&c);
        }
        std::sort (rest.begin(), rest.end(), [] (auto* x, auto* y) { return x->score > y->score; });
        kept += (int) keep.size();
        auto take = keep;
        for (size_t i = 0; (int) take.size() < a.keep && i < rest.size(); ++i)
            take.push_back (rest[i]);
        for (auto* c : take)
        {
            PoolSound s;
            s.archetype = a.id;
            s.preset.info = { c->name, kAuthor, a.category, c->tags, {} };
            s.preset.params = c->params;
            // A kept sound comes back as the listener left it (name, tags, category).
            if (const auto p = state.pathOfId.find (idOf (*c)); p != state.pathOfId.end() && decisionFor (state, idOf (*c)) == "keep")
                if (const auto file = readSound (review.getChildFile (p->second)))
                {
                    s.preset.info.name = file->info.name;
                    s.preset.info.tags = file->info.tags;
                    if (file->info.category.isNotEmpty())
                        s.preset.info.category = file->info.category;
                    s.kept = true;
                }
            s.audio = c->sampleAudio;
            s.slot = c->sample;
            s.features = c->features;
            s.score = c->score;
            pool.push_back (std::move (s));
        }
    }

    // Write the tree from scratch.
    out.deleteRecursively();
    out.createDirectory();
    for (auto& s : pool)
    {
        if (s.slot != nullptr)
        {
            const auto f = out.getChildFile (sampleRelFor (s));
            writeFlac (f, s.audio);
            s.preset.sample = { f.getFullPathName(), f.getSize() };
        }
        const auto file = out.getChildFile (soundPath (s.preset.info.category, s.preset.info.name));
        file.getParentDirectory().createDirectory();
        writeSound (file, s.preset);
        stripAbsolutePaths (file);
    }

    // The neutral kit's sounds, as they always were.
    struct NeutralSound { int voice; const char* category; const char* tags; };
    const NeutralSound neutralSounds[] = {
        { 0, "kick", "warm" },  { 1, "perc", "digital" }, { 2, "snare", "organic" }, { 3, "snare", "organic" },
        { 4, "perc", "warm" },  { 5, "bass", "warm" },    { 6, "hat", "metallic" },  { 7, "hat", "metallic" },
    };
    const auto neutral = defaultKitPreset();
    for (const auto& n : neutralSounds)
    {
        SoundPreset s;
        s.info.name = neutral.names[(size_t) n.voice];
        s.info.author = kAuthor;
        s.info.category = n.category;
        s.info.tags.addTokens (n.tags, ",", "");
        s.params = neutral.voices[(size_t) n.voice];
        const auto file = out.getChildFile (soundPath (n.category, s.info.name));
        file.getParentDirectory().createDirectory();
        writeSound (file, s);
    }

    // Kits.
    std::vector<BuiltKit> kits;
    for (const auto& idea : kitConcepts())
    {
        BuiltKit b;
        b.idea = &idea;
        b.pattern = idea.pattern();
        if (idea.neutral)
        {
            b.kit = neutral;
            b.kit.info.tags = idea.tags;
        }
        else
        {
            b.kit.info = { idea.name, kAuthor, {}, idea.tags, {} };
            b.kit.info.tags.add (idea.style);
            for (int g = 0; g < kNumGlobalParams; ++g)
                b.kit.globals[(size_t) g] = globalParamSpecs()[(size_t) g].def;
            applyGlobals (b.kit.globals, idea.chain);
            b.kit.movement = defaultMovement();
            if (idea.movement)
                idea.movement (b.kit.movement, b.kit.globals);

            std::set<const PoolSound*> inKit;
            for (int v = 0; v < kNumVoices; ++v)
            {
                const auto& slot = idea.slots[(size_t) v];
                const auto* s = pick (pool, slot, inKit);
                if (s == nullptr)
                {
                    std::fprintf (stderr, "%s: nothing for slot %d (%s)\n", idea.name.c_str(), v + 1, slot.name);
                    return 1;
                }
                inKit.insert (s);
                auto p = s->preset.params;
                apply (p, slot.tweak);
                p[vp::Level] = std::clamp (p[vp::Level] + slot.trimDb, -60.0f, 6.0f);
                p[vp::Pan] = slot.pan;
                b.kit.voices[(size_t) v] = p;
                b.kit.names[(size_t) v] = s->preset.info.name;
                b.slots[(size_t) v] = s->slot;
                if (s->slot != nullptr)
                    b.sampleRel[(size_t) v] = sampleRelFor (*s);
            }
        }
        kits.push_back (std::move (b));
    }

    // Levels: every kit plays its pattern as loud as the neutral kit plays the breakbeat.
    const auto& reference = kits.front();
    const auto target = beatLoudnessDb (renderKit (reference.kit, reference.slots, reference.pattern, 120.0f, 0.5f, 4));
    std::printf ("\nkit           loud   peak   chain out | corners: loudness vs centre / peak (00 10 01 11)\n");
    for (auto& b : kits)
    {
        setKitLevel (b, target, true);
        if (! b.idea->neutral)
            storeScenes (b.kit, *b.idea);
    }

    auto writeKitTo = [] (const juce::File& root, const BuiltKit& b, bool factory)
    {
        auto kit = b.kit;
        for (int v = 0; v < kNumVoices; ++v)
            if (b.sampleRel[(size_t) v].isNotEmpty())
            {
                const auto f = root.getChildFile (b.sampleRel[(size_t) v]);
                kit.samples[(size_t) v] = { f.getFullPathName(), f.existsAsFile() ? f.getSize() : -1 };
            }
        const auto file = root.getChildFile ("Kits/" + safeFileName (kit.info.name) + ".batida-kit");
        file.getParentDirectory().createDirectory();
        writeKit (file, kit);
        if (factory)
            stripAbsolutePaths (file);
    };

    std::map<std::string, Pattern> patterns;
    for (const auto& b : kits)
    {
        writeKitTo (out, b, true);
        PatternPreset p;
        p.info = { b.idea->patternName, kAuthor, {}, { b.idea->style }, {} };
        p.pattern = b.pattern;
        patterns[b.idea->patternName] = b.pattern;
        writePattern (out.getChildFile ("Patterns/" + safeFileName (p.info.name) + ".batida-pattern"), p);
    }
    for (const auto& extra : extraPatterns())
    {
        PatternPreset p;
        p.info = { extra.name, kAuthor, {}, { extra.style }, {} };
        p.pattern = extra.pattern();
        patterns[extra.name] = p.pattern;
        writePattern (out.getChildFile ("Patterns/" + safeFileName (p.info.name) + ".batida-pattern"), p);
    }

    std::vector<SetPreset> sets;
    for (const auto& spec : setSpecs())
    {
        const BuiltKit* kit = nullptr;
        for (const auto& b : kits)
            if (b.idea->name == spec.kit)
                kit = &b;
        jassert (kit != nullptr);
        SetPreset set;
        set.info = { spec.name, kAuthor, {}, spec.tags, {} };
        set.kit = kit->kit;
        for (int g = 0; g < kNumGlobalParams; ++g)
            if (! isKitGlobal (g))
                set.kit.globals[(size_t) g] = globalParamSpecs()[(size_t) g].def;
        set.kit.globals[gp::SeqTempo] = spec.tempo;
        set.kit.globals[gp::SeqSwing] = spec.swing;
        for (size_t i = 0; i < spec.patterns.size(); ++i)
            set.patterns.patterns[i] = patterns.at (spec.patterns[i]);
        for (int v = 0; v < kNumVoices; ++v)
            if (kit->sampleRel[(size_t) v].isNotEmpty())
            {
                const auto f = out.getChildFile (kit->sampleRel[(size_t) v]);
                set.kit.samples[(size_t) v] = { f.getFullPathName(), f.getSize() };
            }
        const auto file = out.getChildFile ("Sets/" + safeFileName (spec.name) + ".batida-set");
        file.getParentDirectory().createDirectory();
        writeSet (file, set);
        stripAbsolutePaths (file);
        sets.push_back (set);
    }

    // Fingerprints: how every factory sound measures, read back from its file.
    // The tests check the engine still makes them sound the same.
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        juce::StringArray lines { "# path\tloudness dB\tcentroid Hz\tlength ms\tpitch Hz (written by BatidaFactory build)" };
        juce::Array<juce::File> soundFiles;
        for (const auto& e : juce::RangedDirectoryIterator (out.getChildFile ("Sounds"), true, "*.batida-sound"))
            soundFiles.add (e.getFile());
        soundFiles.sort();
        for (const auto& file : soundFiles)
        {
            const auto sound = readSound (file);
            if (! sound)
                continue;
            SampleSlot slot;
            if (! sound->sample.isEmpty())
                slot.load (juce::File (sound->sample.path), formats);
            const auto f = analyse (sound->params, slot.getDisplayData());
            lines.add (file.getRelativePathFrom (out) + "\t" + juce::String (f.loudnessDb, 2) + "\t" + juce::String (f.centroidHz, 1)
                       + "\t" + juce::String (f.lengthMs, 1) + "\t" + juce::String (f.pitchHz, 1));
        }
        sourceRoot().getChildFile ("resources/factory-fingerprints.txt").replaceWithText (lines.joinIntoString ("\n") + "\n");
    }

    // The factory's version goes up whenever its content changes.
    juce::MemoryBlock content;
    juce::Array<juce::File> files;
    for (const auto& e : juce::RangedDirectoryIterator (out, true, "*", juce::File::findFiles))
        if (e.getFile().getFileName() != "factory-version.txt")
            files.add (e.getFile());
    files.sort();
    for (const auto& f : files)
    {
        content.append (f.getRelativePathFrom (out).toRawUTF8(), f.getRelativePathFrom (out).getNumBytesAsUTF8());
        juce::MemoryBlock bytes;
        f.loadFileAsData (bytes);
        content.append (bytes.getData(), bytes.getSize());
    }
    uint64_t fnv = 14695981039346656037ull; // FNV-1a over names and bytes
    for (size_t i = 0; i < content.getSize(); ++i)
        fnv = (fnv ^ (uint8_t) content[i]) * 1099511628211ull;
    const auto hash = juce::String::toHexString ((juce::int64) fnv);
    const auto versionFile = sourceRoot().getChildFile ("resources/factory-version.txt");
    juce::StringArray previous;
    previous.addTokens (versionFile.loadFileAsString(), " \n", "");
    auto version = previous.size() >= 2 ? previous[0].getIntValue() : 1;
    if (previous.size() < 2 || previous[1] != hash)
        ++version;
    versionFile.replaceWithText (juce::String (version) + " " + hash + "\n");
    out.getChildFile ("factory-version.txt").replaceWithText (juce::String (version) + " " + hash + "\n");

    // Kits, patterns and sets go to Review too, for listening.
    if (review.isDirectory())
    {
        for (const auto& folder : { "Kits", "Patterns", "Sets" })
            review.getChildFile (folder).deleteRecursively();
        for (auto& s : pool) // kits point at Review's copies of their samples
            if (s.slot != nullptr && ! review.getChildFile (sampleRelFor (s)).existsAsFile())
                writeFlac (review.getChildFile (sampleRelFor (s)), s.audio);
        for (const auto& b : kits)
        {
            writeKitTo (review, b, false);
            PatternPreset p;
            p.info = { b.idea->patternName, kAuthor, {}, { b.idea->style }, {} };
            p.pattern = b.pattern;
            writePattern (review.getChildFile ("Patterns/" + safeFileName (p.info.name) + ".batida-pattern"), p);
        }
        for (const auto& extra : extraPatterns())
        {
            PatternPreset p;
            p.info = { extra.name, kAuthor, {}, { extra.style }, {} };
            p.pattern = extra.pattern();
            writePattern (review.getChildFile ("Patterns/" + safeFileName (p.info.name) + ".batida-pattern"), p);
        }
        for (auto set : sets)
        {
            for (int v = 0; v < kNumVoices; ++v)
                if (set.kit.samples[(size_t) v].path.isNotEmpty())
                {
                    const auto rel = juce::File (set.kit.samples[(size_t) v].path).getRelativePathFrom (out);
                    set.kit.samples[(size_t) v].path = review.getChildFile (rel).getFullPathName();
                }
            writeSet (review.getChildFile ("Sets/" + safeFileName (set.info.name) + ".batida-set"), set);
        }
    }

    int sounds = 0;
    for (const auto& e : juce::RangedDirectoryIterator (out.getChildFile ("Sounds"), true, "*.batida-sound"))
        sounds += e.getFile().existsAsFile() ? 1 : 0;
    juce::int64 sampleBytes = 0;
    for (const auto& e : juce::RangedDirectoryIterator (out.getChildFile ("Samples"), true, "*.flac"))
        sampleBytes += e.getFileSize();
    std::printf ("\nfactory %d: %d sounds (%d kept, %d rejected in review), %d kits, %d patterns, %d sets, samples %.1f MB\n",
                 version, sounds, kept, rejected, (int) kits.size(), (int) patterns.size(), (int) sets.size(),
                 (double) sampleBytes / (1024.0 * 1024.0));
    for (const auto& [path, decision] : state.decisions)
        if (decision == "reject" && ! path.startsWith ("Sounds/"))
            std::printf ("  rejected in review, to redo: %s\n", path.toRawUTF8());
    return 0;
}

int runExplain (const juce::String& prefix)
{
    auto all = makeCandidates();
    std::printf ("%-18s %6s %6s %6s %6s %6s %7s %5s %5s %5s  %s\n", "candidate", "rawLd", "rawPk", "level", "loud", "peak", "len", "low", "flat", "inh", "result");
    for (const auto& c : all)
        if (juce::String (c.archetype).startsWith (prefix))
            std::printf ("%-15s#%-2d %6.1f %6.1f %6.1f %6.1f %6.1f %7.0f %5.2f %5.2f %5.2f  %s\n", c.archetype.c_str(), c.index,
                         c.raw.loudnessDb, c.raw.peakDb, c.params[vp::Level], c.features.loudnessDb, c.features.peakDb,
                         c.features.lengthMs, c.features.low, c.features.flatness, c.features.inharmonic,
                         c.dropped.isEmpty() ? "ok" : c.dropped.toRawUTF8());
    return 0;
}

// How Heat plays on the factory kits: each kit with its own pattern at heat
// 0, 0.5 and 1 (loudness relative to heat 0), and its kick alone (how much
// transient it keeps: crest, peak over loudness).
int runHeat (const juce::File& factory)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::printf ("%-10s %7s %7s %7s | kick alone: loud  crest  at heat 0 -> 1\n", "kit", "heat 0", "0.5", "1");
    for (const auto& e : juce::RangedDirectoryIterator (factory.getChildFile ("Kits"), false, "*.batida-kit"))
    {
        const auto kit = readKit (e.getFile());
        const auto pat = readPattern (factory.getChildFile ("Patterns/" + e.getFile().getFileNameWithoutExtension() + ".batida-pattern"));
        if (! kit)
            continue;
        const auto pattern = pat ? pat->pattern : breakbeatPattern();
        std::array<std::shared_ptr<SampleSlot>, kNumVoices> slots;
        for (int v = 0; v < kNumVoices; ++v)
            if (! kit->samples[(size_t) v].isEmpty())
            {
                slots[(size_t) v] = std::make_shared<SampleSlot>();
                slots[(size_t) v]->load (juce::File (kit->samples[(size_t) v].path), formats);
            }
        const auto x = kit->globals[gp::XyX];
        float base = 0.0f;
        std::printf ("%-10s", kit->info.name.toRawUTF8());
        for (const auto y : { 0.0f, 0.5f, 1.0f })
        {
            const auto l = beatLoudnessDb (renderKit (*kit, slots, pattern, 120.0f, 0.5f, 4, { { "xy_x", x }, { "xy_y", y } }));
            if (y == 0.0f)
                base = l;
            std::printf (" %+7.1f", y == 0.0f ? l : l - base);
        }
        auto kickOnly = *kit;
        for (int v = 1; v < kNumVoices; ++v)
            kickOnly.voices[(size_t) v][vp::Level] = -60.0f;
        std::printf (" |");
        for (const auto y : { 0.0f, 1.0f })
        {
            const auto audio = renderKit (kickOnly, slots, pattern, 120.0f, 0.5f, 4, { { "xy_x", x }, { "xy_y", y } });
            const auto l = beatLoudnessDb (audio);
            std::printf ("  %6.1f %5.1f", l, factory::peakDb (audio) - l);
        }
        std::printf ("\n");
    }
    return 0;
}

// Which chain stage makes heat quieter: the kick alone at heat 1 vs 0, with
// each stage's Follow XY off in turn.
int runHeatStages (const juce::File& factory)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (const auto* name : { "Neutral", "Weight", "Slide", "Pressure", "Balance" })
    {
        auto kit = readKit (factory.getChildFile ("Kits/" + juce::String (name) + ".batida-kit"));
        const auto pat = readPattern (factory.getChildFile ("Patterns/" + juce::String (name) + ".batida-pattern"));
        const auto pattern = pat ? pat->pattern : breakbeatPattern();
        for (int v = 1; v < kNumVoices; ++v)
            kit->voices[(size_t) v][vp::Level] = -60.0f;
        std::printf ("%-9s", name);
        for (const auto& [label, off] : { std::pair { "all", "" }, { "no comp", "comp_follow" }, { "no dist", "dist_follow" },
                                          { "no eq", "eq_follow" } })
        {
            auto at = [&] (float y)
            {
                Overrides o { { "xy_y", y } };
                if (juce::String (off).isNotEmpty())
                    o.push_back ({ off, 0.0f });
                return beatLoudnessDb (renderKit (*kit, {}, pattern, 120.0f, 0.5f, 4, o));
            };
            std::printf ("  %s %+5.1f", label, at (1.0f) - at (0.0f));
        }
        std::printf ("\n");
    }
    return 0;
}

int runCalibrate()
{
    const auto kit = defaultKitParams();
    std::printf ("%-12s %7s %7s %7s %8s %5s %5s %5s %5s\n", "sound", "peak", "loud", "len", "centroid", "low", "high", "flat", "inh");
    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto f = analyse (kit.voices[(size_t) v], nullptr);
        std::printf ("%-12s %7.1f %7.1f %7.0f %8.0f %5.2f %5.2f %5.2f %5.2f\n", defaultVoiceName (v), f.peakDb, f.loudnessDb,
                     f.lengthMs, f.centroidHz, f.low, f.high, f.flatness, f.inharmonic);
    }
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    const juce::String command = argc > 1 ? argv[1] : "";
    const juce::File library (argument (argc, argv, "--library", Library::defaultRoot().getFullPathName()));
    const juce::File out (argument (argc, argv, "--out", sourceRoot().getChildFile ("resources/factory").getFullPathName()));

    if (command == "candidates")
        return runCandidates (library);
    if (command == "build")
        return runBuild (library, out);
    if (command == "explain")
        return runExplain (argc > 2 ? argv[2] : "");
    if (command == "heat-stages")
        return runHeatStages (out);
    if (command == "heat")
        return runHeat (out);
    if (command == "calibrate")
        return runCalibrate();
    std::printf ("usage: BatidaFactory candidates|build|calibrate [--library <root>] [--out <folder>]\n");
    return 1;
}
