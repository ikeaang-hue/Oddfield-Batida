#include "Tools.h"

#include "Engine/Kit.h"
#include "Library/Library.h"
#include "Library/SampleLocator.h"

#include <cmath>

namespace batida
{

namespace
{
constexpr double kRate = 48000.0;

// A short WAV file of a sine (or noise), so samples have a real file behind them.
juce::File makeWav (const juce::File& file, float hz, int frames, bool noise = false)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::AudioBuffer<float> b (1, frames);
    juce::Random r (7);
    for (int i = 0; i < frames; ++i)
        b.setSample (0, i, noise ? r.nextFloat() * 2.0f - 1.0f : 0.5f * std::sin (6.2831853f * hz * (float) i / 48000.0f));
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
    auto options = juce::AudioFormatWriterOptions().withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (16);
    if (auto writer = wav.createWriterFor (out, options))
        writer->writeFromAudioSampleBuffer (b, 0, frames);
    return file;
}

std::unique_ptr<SampleData> bufferOf (int frames, bool noise)
{
    auto d = std::make_unique<SampleData>();
    d->audio.setSize (1, frames);
    d->sampleRate = kRate;
    juce::Random r (3);
    for (int i = 0; i < frames; ++i)
        d->audio.setSample (0, i, noise ? r.nextFloat() * 2.0f - 1.0f : 0.5f * std::sin (6.2831853f * 200.0f * (float) i / (float) kRate));
    return d;
}

// A small factory archive (the neutral kit, its 8 sounds, the breakbeat and a
// set), so these tests don't depend on the real factory library.
juce::MemoryBlock smallArchive (const juce::File& temp, int version, bool withBreakbeat = true, const juce::String& id = {})
{
    const auto dir = temp.getChildFile ("archive");
    dir.deleteRecursively();
    const auto kit = defaultKitPreset();
    const char* categories[] = { "kick", "perc", "snare", "snare", "perc", "bass", "hat", "hat" };
    for (int v = 0; v < kNumVoices; ++v)
    {
        SoundPreset s;
        s.info = { kit.names[(size_t) v], "Oddfield", categories[v], { v == 1 ? "synthetic" : "round" }, {} };
        s.params = kit.voices[(size_t) v];
        const auto f = dir.getChildFile ("Sounds/" + Library::categoryFolder (categories[v]) + "/" + s.info.name + ".batida-sound");
        f.getParentDirectory().createDirectory();
        writeSound (f, s);
    }
    dir.getChildFile ("Kits").createDirectory();
    writeKit (dir.getChildFile ("Kits/Neutral.batida-kit"), kit);
    if (withBreakbeat)
    {
        PatternPreset beat;
        beat.info = { "Breakbeat", "Oddfield", {}, { "natural" }, {} };
        beat.pattern = breakbeatPattern();
        dir.getChildFile ("Patterns").createDirectory();
        writePattern (dir.getChildFile ("Patterns/Breakbeat.batida-pattern"), beat);
    }
    SetPreset set;
    set.info = { "Neutral", "Oddfield", {}, { "round" }, {} };
    set.kit = kit;
    set.patterns.patterns[0] = breakbeatPattern();
    dir.getChildFile ("Sets").createDirectory();
    writeSet (dir.getChildFile ("Sets/Neutral.batida-set"), set);
    dir.getChildFile ("factory-version.txt").replaceWithText (juce::String (version) + (id.isNotEmpty() ? " " + id : juce::String()));

    juce::ZipFile::Builder zip;
    for (const auto& e : juce::RangedDirectoryIterator (dir, true, "*", juce::File::findFiles))
        zip.addFile (e.getFile(), 6, e.getFile().getRelativePathFrom (dir));
    juce::MemoryOutputStream out;
    zip.writeToStream (out, nullptr);
    return out.getMemoryBlock();
}

bool sameParams (const VoiceParams& a, const VoiceParams& b)
{
    for (int k = 0; k < kNumVoiceParams; ++k)
        if (a[k] != b[k])
            return false;
    return true;
}

bool samePattern (const Pattern& a, const Pattern& b)
{
    if (a.length != b.length)
        return false;
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& ta = a.tracks[(size_t) t];
        const auto& tb = b.tracks[(size_t) t];
        if (ta.length != tb.length || std::abs (ta.noteLength - tb.noteLength) > 1.0e-6f)
            return false;
        for (int s = 0; s < kMaxSteps; ++s)
        {
            const auto& x = ta.steps[(size_t) s];
            const auto& y = tb.steps[(size_t) s];
            if (x.gate != y.gate || x.velocity != y.velocity || x.pitch != y.pitch || x.slice != y.slice
                || x.ratchet != y.ratchet || x.probability != y.probability)
                return false;
        }
    }
    for (int s = 0; s < kMaxSteps; ++s)
        if (a.xy[(size_t) s].active != b.xy[(size_t) s].active
            || (a.xy[(size_t) s].active && std::abs (a.xy[(size_t) s].x - b.xy[(size_t) s].x) > 1.0e-3f))
            return false;
    return true;
}

// Every parameter moved somewhere odd, so a round trip can't pass by defaults.
VoiceParams oddParams (int seed)
{
    juce::Random r (seed);
    VoiceParams p;
    for (int k = 0; k < kNumVoiceParams; ++k)
    {
        const auto& spec = voiceParamSpecs()[(size_t) k];
        if (spec.kind == Kind::Choice)
            p[k] = (float) r.nextInt ((int) spec.choices.size());
        else if (spec.kind == Kind::Bool)
            p[k] = r.nextBool() ? 1.0f : 0.0f;
        else
            p[k] = spec.min + (spec.max - spec.min) * r.nextFloat();
    }
    return p;
}
} // namespace

class LibraryTests final : public juce::UnitTest
{
public:
    LibraryTests() : juce::UnitTest ("Library", "Batida") {}

    void runTest() override
    {
        const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("BatidaLibraryTests");
        temp.deleteRecursively();
        temp.createDirectory();

        beginTest ("A sound survives save and load exactly");
        {
            SoundPreset s;
            s.info = { "Big Kick", "Tester", "kick", { "round", "bright" }, {} };
            s.params = oddParams (1);
            s.sample.path = makeWav (temp.getChildFile ("sounds/kick.wav"), 60.0f, 4800).getFullPathName();
            const auto file = temp.getChildFile ("sounds/Big Kick.batida-sound");
            expect (writeSound (file, s));
            const auto back = readSound (file);
            expect (back.has_value());
            expect (sameParams (back->params, s.params), "every parameter, exactly");
            expectEquals (back->info.name, juce::String ("Big Kick"));
            expectEquals (back->info.category, juce::String ("kick"));
            expect (back->info.tags == juce::StringArray ({ "round", "bright" }));
            expectEquals (back->sample.path, s.sample.path);
            expectEquals (back->info.madeWith, juce::String (BATIDA_VERSION));
            expect (file.loadFileAsString().contains ("amp_decay=\""), "readable: values by key");
        }

        beginTest ("Tags saved under their old names read as the new ones");
        {
            SoundPreset s;
            s.info = { "Old Kick", "Tester", "kick", { "warm", "neon", "mine", "round" }, {} };
            s.params = oddParams (2);
            const auto file = temp.getChildFile ("sounds/Old Kick.batida-sound");
            expect (writeSound (file, s));
            expect (readSound (file)->info.tags == juce::StringArray ({ "round", "glossy", "mine" }));
        }

        beginTest ("A kit, a pattern and a set survive save and load");
        {
            auto k = defaultKitPreset();
            k.info.name = "Test Kit";
            k.voices[3] = oddParams (2);
            k.names[3] = "Crack";
            k.globals[gp::XyX] = 0.8f;
            k.globals[gp::DistDrive] = 0.61f;
            k.globals[gp::Master] = 3.0f;    // not a kit global
            k.globals[gp::SeqTempo] = 97.0f; // not a kit global
            k.movement.mods[1].targets[0] = { true, false, 2, vp::FltCutoff, -0.4f };
            k.movement.scenes[1].stored = true;
            k.movement.scenes[1].values[gp::DistDrive] = 0.3f;
            k.samples[3].path = makeWav (temp.getChildFile ("kit/crack.wav"), 900.0f, 2400).getFullPathName();
            const auto file = temp.getChildFile ("kit/Test Kit.batida-kit");
            expect (writeKit (file, k));
            const auto back = readKit (file);
            expect (back.has_value());
            expect (sameParams (back->voices[3], k.voices[3]));
            expectEquals (back->names[3], juce::String ("Crack"));
            expectEquals (back->globals[gp::XyX], 0.8f);
            expectEquals (back->globals[gp::DistDrive], 0.61f);
            expectEquals (back->globals[gp::Master], globalParamSpecs()[gp::Master].def, "a kit leaves Master alone");
            expectEquals (back->globals[gp::SeqTempo], globalParamSpecs()[gp::SeqTempo].def, "and the tempo");
            expect (back->movement.mods[1].targets[0] == k.movement.mods[1].targets[0]);
            expect (back->movement.scenes[1].stored);
            expectEquals (back->samples[3].path, k.samples[3].path);

            PatternPreset p { { "Beat", "", "", {}, {} }, breakbeatPattern() };
            const auto pf = temp.getChildFile ("Beat.batida-pattern");
            expect (writePattern (pf, p));
            const auto pb = readPattern (pf);
            expect (pb.has_value() && samePattern (pb->pattern, p.pattern), "pattern: every step and lane");

            SetPreset set;
            set.info.name = "Everything";
            set.kit = k;
            set.patterns.patterns[5] = breakbeatPattern();
            const auto sf = temp.getChildFile ("Everything.batida-set");
            expect (writeSet (sf, set));
            const auto sb = readSet (sf);
            expect (sb.has_value());
            expectEquals (sb->kit.globals[gp::Master], 3.0f, "a set holds every global");
            expectEquals (sb->kit.globals[gp::SeqTempo], 97.0f);
            expect (samePattern (sb->patterns.patterns[5], set.patterns.patterns[5]));
            expect (sb->patterns.patterns[0].isEmpty());
        }

        beginTest ("Wrong type, not a Batida file, newer format");
        {
            juce::String error;
            expect (! readKit (temp.getChildFile ("sounds/Big Kick.batida-sound"), &error).has_value());
            expect (error.contains ("not a kit"), error);
            const auto junk = temp.getChildFile ("junk.batida-sound");
            junk.replaceWithText ("hello");
            expect (! readSound (junk, &error).has_value());

            // A file from a later Batida: unknown parts are ignored, known ones read.
            const auto future = temp.getChildFile ("future.batida-sound");
            future.replaceWithText ("<BATIDA type=\"sound\" format=\"99\"><INFO name=\"Later\"/><HOLOGRAM x=\"1\"/>"
                                    "<VOICE amp_decay=\"123\" warp_factor=\"9\"/></BATIDA>");
            const auto f = readSound (future);
            expect (f.has_value());
            expectEquals (f->info.format, 99);
            expectEquals (f->params[vp::AmpD], 123.0f);
            expectEquals (f->params[vp::FltCutoff], voiceParamSpecs()[vp::FltCutoff].def, "missing values: defaults");
        }

        beginTest ("A kit folder moved elsewhere still finds its samples");
        {
            const auto a = temp.getChildFile ("MoveA");
            auto k = defaultKitPreset();
            k.samples[0].path = makeWav (a.getChildFile ("Kick Samples/deep.wav"), 50.0f, 4800).getFullPathName();
            expect (writeKit (a.getChildFile ("Mine.batida-kit"), k));
            const auto b = temp.getChildFile ("MoveB");
            expect (a.moveFileTo (b));
            const auto back = readKit (b.getChildFile ("Mine.batida-kit"));
            expect (back.has_value());
            expectEquals (back->samples[0].path, b.getChildFile ("Kick Samples/deep.wav").getFullPathName(),
                          "relative path first");
        }

        beginTest ("Collecting copies samples once; finding locates moved ones");
        {
            const auto src = makeWav (temp.getChildFile ("src/hit.wav"), 440.0f, 3000);
            const auto dest = temp.getChildFile ("collected");
            const auto c1 = collectSample (src, dest);
            expect (c1.existsAsFile() && c1.hasIdenticalContentTo (src));
            expect (collectSample (src, dest) == c1, "the same file isn't copied twice");
            const auto other = makeWav (temp.getChildFile ("src2/hit.wav"), 880.0f, 5000);
            const auto c2 = collectSample (other, dest);
            expectEquals (c2.getFileName(), juce::String ("hit 2.wav"), "a different file with the same name");

            // A decoy with the same name but another size, and the real one deeper down.
            makeWav (temp.getChildFile ("search/a/hit.wav"), 100.0f, 1000);
            const auto real = temp.getChildFile ("search/b/c/hit.wav");
            real.getParentDirectory().createDirectory();
            expect (src.copyFileTo (real));
            const auto found = findSample ("hit.wav", src.getSize(), { temp.getChildFile ("search") });
            expect (found == real, found.getFullPathName());
            expect (findSample ("nothing.wav", -1, { temp.getChildFile ("search") }) == juce::File());
        }

        beginTest ("Factory install: written once, never touches User");
        const auto archive1 = smallArchive (temp, 1);
        Library::setFactoryArchive (archive1.getData(), archive1.getSize());
        {
            const auto root = temp.getChildFile ("Lib");
            Library lib (root, temp.getChildFile ("settings.xml"));
            expect (lib.installFactory(), "first install writes");
            expect (! root.getChildFile ("Factory/factory-version.txt").exists(), "the version stays out of the library");
            expect (root.getChildFile ("Factory/Kits/Neutral.batida-kit").existsAsFile());
            expect (root.getChildFile ("Factory/Sounds/Kick/Kick.batida-sound").existsAsFile());
            expect (root.getChildFile ("Factory/Sounds/Hat/Closed Hat.batida-sound").existsAsFile());
            expect (root.getChildFile ("Factory/Patterns/Breakbeat.batida-pattern").existsAsFile());
            expect (root.getChildFile ("Factory/Sets/Neutral.batida-set").existsAsFile());
            expect (! lib.installFactory(), "second install does nothing");

            const auto mine = lib.userFileFor (PresetType::Sound, { "My Kick", "", "kick", {}, {} });
            expectEquals (mine.getFullPathName(), root.getChildFile ("User/Sounds/Kick/My Kick.batida-sound").getFullPathName());
            SoundPreset s;
            s.info = { "My Kick", "Me", "kick", { "bright" }, {} };
            s.params = oddParams (3);
            expect (writeSound (mine, s));
            const auto before = mine.loadFileAsString();
            root.getChildFile ("Factory/.version").replaceWithText ("0"); // an older factory
            expect (lib.installFactory(), "an older factory is refreshed");
            expectEquals (mine.loadFileAsString(), before, "User untouched");

            const auto kit = readKit (root.getChildFile ("Factory/Kits/Neutral.batida-kit"));
            expect (kit.has_value());
            expect (sameParams (kit->voices[0], defaultKitParams().voices[0]), "the factory kit is the default kit");
        }

        beginTest ("Index, filters, search and favourites");
        {
            const auto root = temp.getChildFile ("Lib");
            Library lib (root, temp.getChildFile ("settings.xml"));
            lib.scanNow();
            auto names = [] (const std::vector<LibraryEntry>& list)
            {
                juce::StringArray n;
                for (const auto& e : list)
                    n.add (e.info.name);
                return n;
            };
            LibraryFilter all;
            const auto sounds = lib.filtered (PresetType::Sound, all);
            expectEquals ((int) sounds.size(), 9, "8 factory sounds and 1 user sound");
            expectEquals (sounds.front().info.category, juce::String ("kick"), "sorted by category");

            LibraryFilter f;
            f.category = "hat";
            expect (names (lib.filtered (PresetType::Sound, f)) == juce::StringArray ({ "Closed Hat", "Open Hat" }));
            f = {};
            f.tags = { "bright" };
            expect (names (lib.filtered (PresetType::Sound, f)) == juce::StringArray ({ "My Kick" }));
            f = {};
            f.source = LibraryFilter::Source::User;
            expectEquals ((int) lib.filtered (PresetType::Sound, f).size(), 1);
            f = {};
            f.search = "open hat";
            expect (names (lib.filtered (PresetType::Sound, f)) == juce::StringArray ({ "Open Hat" }), "every word must match");
            f.search = "oddfield";
            expectEquals ((int) lib.filtered (PresetType::Sound, f).size(), 8, "search covers the author");
            expectEquals ((int) lib.filtered (PresetType::Kit, all).size(), 1);
            expectEquals ((int) lib.filtered (PresetType::Pattern, all).size(), 1);
            expectEquals ((int) lib.filtered (PresetType::Set, all).size(), 1);

            const auto snare = root.getChildFile ("Factory/Sounds/Snare/Snare.batida-sound");
            lib.setFavourite (snare, true);
            f = {};
            f.source = LibraryFilter::Source::Favourites;
            expect (names (lib.filtered (PresetType::Sound, f)) == juce::StringArray ({ "Snare" }));
            Library again (root, temp.getChildFile ("settings.xml"));
            expect (again.isFavourite (snare), "favourites are kept");

            // Tags can be edited on User files, not factory ones.
            const auto mine = root.getChildFile ("User/Sounds/Kick/My Kick.batida-sound");
            expect (lib.updateInfo (mine, { "My Kick", "Me", "kick", { "bright", "synthetic" }, {} }));
            expect (readSound (mine)->info.tags.contains ("synthetic"));
            expect (! lib.updateInfo (snare, { "Snare", "", "snare", { "bright" }, {} }));
        }

        beginTest ("A newer factory replaces the old one whole, and leaves User and favourites alone");
        {
            const auto root = temp.getChildFile ("Lib");
            Library lib (root, temp.getChildFile ("settings.xml"));
            lib.scanNow();
            const auto snare = root.getChildFile ("Factory/Sounds/Snare/Snare.batida-sound");
            expect (lib.isFavourite (snare));
            const auto user = root.getChildFile ("User/Sounds/Kick/My Kick.batida-sound");
            const auto before = user.loadFileAsString();
            expect (! lib.installFactory(), "the same version: nothing to do");

            const auto archive2 = smallArchive (temp, 2, false); // version 2 drops the breakbeat
            Library::setFactoryArchive (archive2.getData(), archive2.getSize());
            expect (lib.installFactory(), "a newer archive installs");
            expect (! root.getChildFile ("Factory/Patterns/Breakbeat.batida-pattern").exists(), "files the new factory dropped go");
            expect (root.getChildFile ("Factory/Kits/Neutral.batida-kit").existsAsFile());
            expectEquals (root.getChildFile ("Factory/.version").loadFileAsString().trim(), juce::String ("2"));
            expectEquals (user.loadFileAsString(), before, "User untouched");
            expect (lib.isFavourite (snare), "favourites kept");
            // The same number with other content (a factory rebuilt in place) installs too.
            const auto archive2b = smallArchive (temp, 2, true);
            Library::setFactoryArchive (archive2b.getData(), archive2b.getSize());
            expect (! lib.installFactory(), "same number, same content: nothing to do");
            const auto archive2c = smallArchive (temp, 2, true, "other");
            Library::setFactoryArchive (archive2c.getData(), archive2c.getSize());
            expect (lib.installFactory(), "same number, other content: installs");
            expect (root.getChildFile ("Factory/Patterns/Breakbeat.batida-pattern").existsAsFile());
            const auto archive1old = smallArchive (temp, 1, true, "older");
            Library::setFactoryArchive (archive1old.getData(), archive1old.getSize());
            expect (! lib.installFactory(), "never over a newer factory");

            Library::setFactoryArchive (archive1.getData(), archive1.getSize());
            root.getChildFile ("Factory/.version").replaceWithText ("0");
            lib.installFactory(); // back to the small factory for the tests below
        }

        beginTest ("Review: candidates, keep and reject");
        {
            const auto root = temp.getChildFile ("Lib");
            SoundPreset s;
            s.info = { "Candidate", "Oddfield", "kick", { "round" }, {} };
            s.params = oddParams (5);
            const auto file = root.getChildFile ("Review/Sounds/Kick/Candidate.batida-sound");
            file.getParentDirectory().createDirectory();
            expect (writeSound (file, s));

            Library lib (root, temp.getChildFile ("settings.xml"));
            lib.scanNow();
            LibraryFilter all, review;
            review.source = LibraryFilter::Source::Review;
            bool inAll = false;
            for (const auto& e : lib.filtered (PresetType::Sound, all))
                inAll = inAll || e.file == file;
            expect (! inAll, "candidates stay out of All");
            expectEquals ((int) lib.filtered (PresetType::Sound, review).size(), Library::kReviewBuild ? 1 : 0,
                          "candidates show under Review, in review builds only");
            expect (lib.hasReview() == Library::kReviewBuild);

            expect (lib.getDecision (file) == Decision::None);
            lib.setDecision (file, Decision::Keep);
            expect (lib.getDecision (file) == Decision::Keep);
            const auto json = juce::JSON::parse (root.getChildFile ("Review/decisions.json"));
            expectEquals (json["Sounds/Kick/Candidate.batida-sound"].toString(), juce::String ("keep"));
            Library again (root, temp.getChildFile ("settings.xml"));
            expect (again.getDecision (file) == Decision::Keep, "decisions are kept");
            again.setDecision (file, Decision::Reject);
            expect (again.getDecision (file) == Decision::Reject);
            again.setDecision (file, Decision::None);
            expect (again.getDecision (file) == Decision::None);
            expect (lib.getDecision (root.getChildFile ("User/Sounds/Kick/My Kick.batida-sound")) == Decision::None,
                    "only Review files have decisions");
            root.getChildFile ("Review").deleteRecursively();
        }

        beginTest ("Settings: author, folders");
        {
            const auto settings = temp.getChildFile ("settings2.xml");
            {
                Library lib (temp.getChildFile ("Lib"), settings);
                lib.setAuthor ("Q");
                lib.addSampleFolder (temp.getChildFile ("src"));
                lib.addRelinkFolder (temp.getChildFile ("src2"));
            }
            Library lib (temp.getChildFile ("Lib"), settings);
            expectEquals (lib.getAuthor(), juce::String ("Q"));
            expect (lib.getSampleFolders().contains (temp.getChildFile ("src")));
            expect (lib.searchFolders().contains (temp.getChildFile ("src2")));
            expect (lib.searchFolders().contains (temp.getChildFile ("Lib")));
        }

        beginTest ("Guessing a category from a name");
        {
            expectEquals (guessCategory ("Big Kick"), juce::String ("kick"));
            expectEquals (guessCategory ("Closed Hat"), juce::String ("hat"));
            expectEquals (guessCategory ("Clap"), juce::String ("snare"));
            expectEquals (guessCategory ("Sub Bass"), juce::String ("bass"));
            expectEquals (guessCategory ("Rim"), juce::String ("perc"));
            expectEquals (guessCategory ("Thing"), juce::String());
            expectEquals (safeFileName ("Kick / 909?"), juce::String ("Kick - 909"));
        }

        beginTest ("Switching a sounding voice's sound is click-free");
        {
            // A sample voice playing a sine; its sample and settings are replaced
            // mid-note by loud noise. Without the fade the output would jump.
            auto run = [&] (bool fade, float& worstJump, float& afterPeak)
            {
                Kit kit;
                kit.prepare (kRate, 256);
                auto params = defaultKitParams();
                params.global[gp::Master] = 0.0f;
                auto& v = params.voices[0];
                v[vp::SrcMode] = (float) SourceMode::Sample;
                v[vp::ChainAmt] = 0.0f; // dry, so only the voice is measured
                v[vp::AmpD] = 4000.0f;
                kit.sampleSlot (0).setData (bufferOf (48000, false));
                juce::AudioBuffer<float> out (2, 256);
                juce::MidiBuffer none;
                kit.setParameters (params);
                kit.process (out, none); // the voice picks up its sample
                kit.noteOn (0, Voice::kBaseKey, 1.0f);
                for (int b = 0; b < 20; ++b)
                {
                    kit.setParameters (params);
                    kit.process (out, none);
                }
                float last = out.getSample (0, 255);
                worstJump = 0.0f;
                if (fade)
                    kit.beginSwitch (1u);
                kit.sampleSlot (0).setData (bufferOf (48000, true));
                params.voices[0][vp::Level] = 6.0f;
                for (int b = 0; b < 4; ++b)
                {
                    kit.setParameters (params);
                    kit.process (out, none);
                    for (int i = 0; i < 256; ++i)
                    {
                        worstJump = std::max (worstJump, std::abs (out.getSample (0, i) - last));
                        last = out.getSample (0, i);
                    }
                }
                // A hit after the switch plays the new sound.
                kit.noteOn (0, Voice::kBaseKey, 1.0f);
                afterPeak = 0.0f;
                for (int b = 0; b < 4; ++b)
                {
                    kit.setParameters (params);
                    kit.process (out, none);
                    afterPeak = std::max (afterPeak, out.getMagnitude (0, 0, 256));
                }
            };
            // Without beginSwitch the new sample alone triggers the same fade
            // (loads decode first, so the sample can land in any order).
            float jumpFade = 0, peakFade = 0, jumpSample = 0, peakSample = 0;
            run (true, jumpFade, peakFade);
            run (false, jumpSample, peakSample);
            logMessage ("  largest step with beginSwitch " + juce::String (jumpFade, 4) + ", sample change only " + juce::String (jumpSample, 4));
            expectLessThan (jumpFade, 0.05f, "no click");
            expectLessThan (jumpSample, 0.05f, "no click when only the sample changes");
            expectGreaterThan (peakFade, 0.3f, "the next hit plays the new sound");
            expectGreaterThan (peakSample, 0.3f, "the next hit plays the new sample");
        }

        beginTest ("A hit during the fade waits for the new sound");
        {
            Kit kit;
            kit.prepare (kRate, 64);
            auto params = defaultKitParams();
            kit.setParameters (params);
            kit.noteOn (0, Voice::kBaseKey, 1.0f);
            juce::AudioBuffer<float> out (2, 64);
            juce::MidiBuffer none;
            kit.process (out, none);
            kit.beginSwitch (1u);
            kit.setParameters (params);
            expect (kit.isSwitching (0));
            kit.noteOn (0, Voice::kBaseKey, 1.0f); // arrives mid-fade
            for (int b = 0; b < 4; ++b)
            {
                kit.setParameters (params);
                kit.process (out, none);
            }
            expect (! kit.isSwitching (0));
            expect (kit.getVoice (0).isActive(), "the waiting hit played");
        }

        temp.deleteRecursively();
    }
};

static LibraryTests libraryTests;

} // namespace batida
