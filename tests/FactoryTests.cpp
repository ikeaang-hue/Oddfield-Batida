#include "Tools.h"

#include "Factory.h"
#include "Library/Library.h"

#include <FactoryData.h>

// The factory library (SPEC §11), as built into the plugin: every file loads
// and plays cleanly, the levels hold, and the engine still makes every sound
// the way it was kept (the fingerprints written by BatidaFactory build).

namespace batida
{

namespace
{
struct LoadedKit
{
    KitPreset kit;
    std::array<std::shared_ptr<SampleSlot>, kNumVoices> slots;
};

LoadedKit loadKit (const KitPreset& kit, juce::AudioFormatManager& formats)
{
    LoadedKit k { kit, {} };
    for (int v = 0; v < kNumVoices; ++v)
        if (! kit.samples[(size_t) v].isEmpty())
        {
            k.slots[(size_t) v] = std::make_shared<SampleSlot>();
            k.slots[(size_t) v]->load (juce::File (kit.samples[(size_t) v].path), formats);
        }
    return k;
}
} // namespace

class FactoryTests final : public juce::UnitTest
{
public:
    FactoryTests() : juce::UnitTest ("Factory library", "Batida") {}

    void runTest() override
    {
        using namespace factory;
        Library::setFactoryArchive (FactoryData::Factory_zip, (size_t) FactoryData::Factory_zipSize);
        const auto temp = juce::File::createTempFile ("batida-factory");
        temp.createDirectory();
        const auto root = temp.getChildFile ("Lib");
        Library lib (root, temp.getChildFile ("settings.xml"));
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        beginTest ("The factory installs whole");
        expect (lib.installFactory());
        lib.scanNow();
        LibraryFilter all;
        const auto sounds = lib.filtered (PresetType::Sound, all);
        const auto kits = lib.filtered (PresetType::Kit, all);
        const auto patterns = lib.filtered (PresetType::Pattern, all);
        const auto sets = lib.filtered (PresetType::Set, all);
        expect (sounds.size() >= 150, juce::String ((int) sounds.size()) + " sounds");
        expectEquals ((int) kits.size(), 40, "kits");
        expectEquals ((int) patterns.size(), 58, "patterns");
        expectEquals ((int) sets.size(), 19, "sets");
        expect (Library::archiveVersion() >= 2, "a version-2 factory or newer");
        expect (FactoryData::Factory_zipSize < 12 * 1024 * 1024, "the archive stays small");
        for (const auto* name : { "Kits/Neutral.batida-kit", "Sounds/Kick/Kick.batida-sound", "Patterns/Breakbeat.batida-pattern",
                                  "Sets/Neutral Breakbeat.batida-set" })
            expect (root.getChildFile ("Factory").getChildFile (name).existsAsFile(), name);

        beginTest ("Styles are found by search");
        for (const auto& [style, least] : { std::pair { "techno", 8 }, { "breaks", 7 }, { "glitch", 5 }, { "house", 5 },
                                            { "garage", 3 }, { "hip hop", 3 }, { "trap", 4 } })
        {
            LibraryFilter f;
            f.search = style;
            expect ((int) lib.filtered (PresetType::Kit, f).size() >= least, style);
            expect ((int) lib.filtered (PresetType::Pattern, f).size() >= least, style);
        }

        beginTest ("Each mood has its sounds, a kit and a set");
        for (const auto* mood : { "neutral", "synth-aggressive", "neon", "tender" })
        {
            LibraryFilter f;
            f.search = mood;
            expect ((int) lib.filtered (PresetType::Sound, f).size() >= 12, mood);
            expect ((int) lib.filtered (PresetType::Kit, f).size() >= 1, mood);
            expect ((int) lib.filtered (PresetType::Set, f).size() >= 1, mood);
        }

        beginTest ("Every sound loads and plays cleanly, and sounds as it was kept");
        {
            std::map<juce::String, juce::StringArray> prints;
            const auto printFile = juce::File (BATIDA_SOURCE_DIR).getChildFile ("resources/factory-fingerprints.txt");
            juce::StringArray lines;
            lines.addLines (printFile.loadFileAsString());
            for (const auto& line : lines)
                if (! line.startsWith ("#") && line.isNotEmpty())
                {
                    juce::StringArray cells;
                    cells.addTokens (line, "\t", "");
                    prints[cells[0]] = cells;
                }
            expectEquals ((int) prints.size(), (int) sounds.size(), "a fingerprint for every sound");

            int moved = 0;
            for (const auto& e : sounds)
            {
                const auto sound = readSound (e.file);
                expect (sound.has_value(), e.file.getFileName());
                if (! sound)
                    continue;
                SampleSlot slot;
                if (! sound->sample.isEmpty())
                    expect (slot.load (juce::File (sound->sample.path), formats), "sample for " + sound->info.name);
                const auto f = analyse (sound->params, slot.getDisplayData());
                const auto what = e.info.name + " (" + e.info.category + ")";
                expect (f.finite, what + " is finite");
                expect (f.peakDb > -40.0f, what + " makes sound");
                expect (f.peakDb < 3.5f, what + " peaks " + juce::String (f.peakDb, 1) + " dB");
                expect (soundCategories().contains (e.info.category), what + " has a category");
                expect (! e.info.tags.isEmpty(), what + " has a character");

                const auto rel = e.file.getRelativePathFrom (root.getChildFile ("Factory"));
                const auto it = prints.find (rel);
                if (it == prints.end())
                {
                    expect (false, "no fingerprint for " + rel);
                    continue;
                }
                const auto& p = it->second;
                const auto loud = p[1].getFloatValue(), centroid = p[2].getFloatValue(), length = p[3].getFloatValue();
                const auto same = std::abs (f.loudnessDb - loud) < 1.0f
                               && std::abs (std::log2 ((f.centroidHz + 50.0f) / (centroid + 50.0f))) < 0.2f
                               && std::abs (f.lengthMs - length) < std::max (10.0f, 0.2f * length);
                if (! same)
                {
                    ++moved;
                    logMessage ("  moved: " + rel + " loudness " + juce::String (f.loudnessDb, 1) + " (" + juce::String (loud, 1)
                                + "), centroid " + juce::String (f.centroidHz, 0) + " (" + juce::String (centroid, 0) + "), length "
                                + juce::String (f.lengthMs, 0) + " (" + juce::String (length, 0) + ")");
                }
            }
            expectEquals (moved, 0, "factory sounds the engine now makes differently (rebuild the factory if meant)");
        }

        beginTest ("Every set plays its patterns cleanly across the pad");
        for (const auto& e : sets)
        {
            const auto set = readSet (e.file);
            expect (set.has_value(), e.file.getFileName());
            if (! set)
                continue;
            const auto k = loadKit (set->kit, formats);
            const auto tempo = set->kit.globals[gp::SeqTempo], swing = set->kit.globals[gp::SeqSwing];
            for (int p = 0; p < kNumPatterns; ++p)
            {
                const auto& pattern = set->patterns.patterns[(size_t) p];
                if (pattern.isEmpty())
                    continue;
                for (const auto& [x, y] : { std::pair { -1.0f, -1.0f }, { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }, { 1.0f, 0.0f } })
                {
                    Overrides pad;
                    if (x >= 0.0f)
                        pad = { { "xy_x", x }, { "xy_y", y } };
                    const auto audio = renderKit (k.kit, k.slots, pattern, tempo, swing, 2, pad);
                    const auto loud = beatLoudnessDb (audio), peak = factory::peakDb (audio);
                    const auto what = e.info.name + " pattern " + juce::String (p + 1) + " at " + juce::String (x) + "," + juce::String (y);
                    expect (peak <= 0.01f, what + " peaks " + juce::String (peak, 2));
                    expect (loud > -26.0f && loud < -11.0f, what + " loudness " + juce::String (loud, 1));
                }
            }
        }

        // Every pattern in five kits spread over the list (all of them would
        // take minutes); the sets above play each pattern in its own kit.
        beginTest ("Every pattern makes sound in other kits");
        {
            std::vector<LoadedKit> loaded;
            for (const auto& e : kits)
                if (const auto kit = readKit (e.file))
                    loaded.push_back (loadKit (*kit, formats));
            expectEquals ((int) loaded.size(), (int) kits.size(), "every kit loads");
            for (const auto& e : patterns)
            {
                const auto p = readPattern (e.file);
                expect (p.has_value(), e.file.getFileName());
                if (! p)
                    continue;
                const auto index = (size_t) (&e - patterns.data());
                for (size_t n = 0; n < 5 && ! loaded.empty(); ++n)
                {
                    const auto& k = loaded[(index * 7 + n * 5) % loaded.size()];
                    const auto audio = renderKit (k.kit, k.slots, p->pattern, 120.0f, 0.5f, 1);
                    const auto peak = factory::peakDb (audio);
                    expect (peak > -40.0f && peak <= 0.01f, e.info.name + " in " + k.kit.info.name + " peaks " + juce::String (peak, 1));
                }
            }
        }

        temp.deleteRecursively();
    }
};

static FactoryTests factoryTests;

} // namespace batida
