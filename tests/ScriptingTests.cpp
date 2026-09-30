#include "Analysis/Measure.h"
#include "Cli.h"
#include "Engine/Render.h"
#include "Library/Library.h"
#include "Library/Reference.h"
#include "Library/Validate.h"
#include "Plugin/PluginProcessor.h"

#include <cmath>
#include <cstdlib>
#include <sstream>

// Phase 9, files and scripting (SPEC 1.16): the reference, the batida tool,
// validate, forgiving files, the User-folder check and the reload.

namespace batida
{

namespace
{
constexpr double kRate = 48000.0;

juce::File scratch()
{
    static const auto dir = []
    {
        auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("BatidaScriptingTests");
        d.deleteRecursively();
        d.createDirectory();
        return d;
    }();
    return dir;
}

juce::File writeText (const juce::String& name, const juce::String& text)
{
    const auto f = scratch().getChildFile (name);
    f.getParentDirectory().createDirectory();
    f.replaceWithText (text);
    return f;
}

struct Result
{
    int code = 0;
    juce::String out, err;
    juce::var json() const { return juce::JSON::parse (out); }
};

Result batidaCli (const juce::StringArray& args)
{
    std::ostringstream out, err;
    Result r;
    r.code = cli::run (args, out, err);
    r.out = juce::String::fromUTF8 (out.str().c_str());
    r.err = juce::String::fromUTF8 (err.str().c_str());
    return r;
}

juce::AudioBuffer<float> sine (float hz, float amplitude, double rate, double seconds, int channels = 1, float phase = 0.0f)
{
    juce::AudioBuffer<float> b (channels, (int) (seconds * rate));
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (ch, i, amplitude * std::sin (phase + juce::MathConstants<float>::twoPi * hz * (float) i / (float) rate));
    return b;
}

bool hasProblem (const Validation& v, const juce::String& text)
{
    for (const auto& p : v.problems)
        if (p.what.contains (text) || p.where.contains (text))
            return true;
    return false;
}

juce::String problemsOf (const Validation& v)
{
    juce::StringArray s;
    for (const auto& p : v.problems)
        s.add (p.where + ": " + p.what);
    return s.joinIntoString ("; ");
}

// Runs the message loop until `done` or the time is up.
bool waitFor (std::function<bool()> done, int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
    {
        if (done())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }
    return done();
}

juce::String soundFile (const juce::String& voiceAttributes)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<BATIDA type=\"sound\">\n  <VOICE " + voiceAttributes + "/>\n</BATIDA>\n";
}
} // namespace

class ScriptingTests final : public juce::UnitTest
{
public:
    ScriptingTests() : juce::UnitTest ("Files and scripting", "Batida") {}

    void runTest() override
    {
        beginTest ("The reference matches the code");
        {
            // docs/REFERENCE.md is generated; BATIDA_WRITE_GOLDEN=1 rewrites it.
            const auto doc = juce::File (BATIDA_SOURCE_DIR).getChildFile ("docs/REFERENCE.md");
            const auto text = referenceMarkdown();
            if (juce::SystemStats::getEnvironmentVariable ("BATIDA_WRITE_GOLDEN", {}) == "1")
                doc.replaceWithText (text, false, false, "\n");
            expect (doc.loadFileAsString() == text, "docs/REFERENCE.md is out of date: run the tests with BATIDA_WRITE_GOLDEN=1");
            for (int p = 0; p < kNumVoiceParams; ++p)
                expect (describeVoiceParam (p).isNotEmpty(), "a description for " + juce::String (voiceParamSpecs()[(size_t) p].key));
            for (int g = 0; g < kNumGlobalParams; ++g)
                expect (describeGlobalParam (g).isNotEmpty(), "a description for " + juce::String (globalParamID (g)));
            expectEquals (parameterData().size(), kNumVoiceParams + kNumGlobalParams);
            expect (text.contains ("`amp_decay`") && text.contains ("`opN_ratio`") && text.contains ("`modN_mode`"));
        }

        beginTest ("The reference's smallest files validate, load and play");
        {
            for (auto t : { PresetType::Sound, PresetType::Kit, PresetType::Pattern, PresetType::Set })
            {
                const auto f = writeText ("examples/Example" + juce::String (extensionFor (t)), exampleFile (t));
                const auto v = validateFile (f);
                expect (v.clean(), juce::String (typeName (t)) + ": " + problemsOf (v));
                const auto wav = scratch().getChildFile ("examples/" + juce::String (typeName (t)) + ".wav");
                juce::StringArray args { "render", f.getFullPathName(), "-o", wav.getFullPathName() };
                if (t == PresetType::Kit)
                    args.addArray ({ "--sound", "3" }); // the snare
                const auto r = batidaCli (args);
                expectEquals (r.code, 0, juce::String (typeName (t)) + " renders: " + r.err + r.out);
                expect (wav.existsAsFile());
            }
            const auto sound = readSound (scratch().getChildFile ("examples/Example.batida-sound"));
            expect (sound.has_value());
            expectEquals (sound->params.choice (vp::DriveType), (int) DriveType::Soft, "\"soft\" reads as Soft");
            expectWithinAbsoluteError (sound->params[vp::AmpD], 450.0f, 0.01f);
            expectWithinAbsoluteError (sound->params[vp::AmpR], voiceParamSpecs()[vp::AmpR].def, 0.0f, "missing settings: defaults");
            const auto kit = readKit (scratch().getChildFile ("examples/Example.batida-kit"));
            expect (kit.has_value());
            expectEquals (kit->names[1], juce::String (defaultVoiceName (1)), "a slot with no VOICE keeps the Neutral kit's sound");
            expectWithinAbsoluteError (kit->voices[2][vp::AmpD], 180.0f, 0.01f);
            const auto pattern = readPattern (scratch().getChildFile ("examples/Example.batida-pattern"));
            expect (pattern.has_value());
            const auto& kick = pattern->pattern.tracks[0];
            expect (kick.steps[0].gate && ! kick.steps[1].gate && kick.steps[4].gate && kick.steps[12].gate, "x... reads as hits and rests");
            expectEquals ((int) pattern->pattern.tracks[2].steps[4].velocity, 120);
            expectEquals ((int) pattern->pattern.tracks[2].steps[5].velocity, 100, "a short lane: the rest default");
        }

        beginTest ("Forgiving values");
        {
            const auto& drive = voiceParamSpecs()[vp::DriveType];
            expectEquals ((int) valueFrom (drive, "HARD"), (int) DriveType::Hard);
            expectEquals ((int) valueFrom (drive, " fold "), (int) DriveType::Fold);
            expectEquals ((int) valueFrom (drive, "3"), 3);
            const auto& mute = voiceParamSpecs()[vp::Mute];
            for (auto* on : { "1", "true", "On", "YES" })
                expectEquals (valueFrom (mute, on), 1.0f, on);
            for (auto* off : { "0", "false", "off", "no" })
                expectEquals (valueFrom (mute, off), 0.0f, off);
            expectEquals (valueFrom (voiceParamSpecs()[vp::AmpD], "99999"), 8000.0f, "clamped");
        }

        beginTest ("Saved files point to the reference");
        {
            const auto f = scratch().getChildFile ("comment/Kick.batida-sound");
            expect (writeSound (f, initSoundPreset()));
            const auto text = f.loadFileAsString();
            expect (text.startsWith ("<?xml") && text.contains (kReferenceUrl) && text.contains ("batida params"));
            expect (text.indexOf ("<!--") < text.indexOf ("<BATIDA"));
            expect (readSound (f).has_value(), "still loads");
            expect (validateFile (f).clean());
        }

        beginTest ("Validate finds what loading lets slide");
        {
            auto check = [&] (const juce::String& name, const juce::String& text, const juce::String& expected, bool fatal = false)
            {
                const auto v = validateFile (writeText ("validate/" + name, text));
                expect (hasProblem (v, expected), name + ": expected \"" + expected + "\", got: " + problemsOf (v));
                expect (v.loads() != fatal, name + (fatal ? " doesn't load" : " still loads"));
            };
            check ("typo.batida-sound", soundFile ("flt_cuttoff=\"800\""), "did you mean flt_cutoff");
            check ("range.batida-sound", soundFile ("amp_decay=\"99999\""), "clamped to 8000");
            check ("choice.batida-sound", soundFile ("drive_type=\"Warm\""), "isn't a choice");
            check ("switch.batida-sound", soundFile ("mute=\"maybe\""), "isn't a switch value");
            check ("number.batida-sound", soundFile ("level=\"loud\""), "isn't a number");
            check ("category.batida-sound", "<BATIDA type=\"sound\"><INFO category=\"drums\"/><VOICE/></BATIDA>", "isn't a category");
            check ("element.batida-sound", "<BATIDA type=\"sound\"><VOICES/></BATIDA>", "unknown element");
            check ("sample.batida-sound", "<BATIDA type=\"sound\"><VOICE src_mode=\"Sample\"><SAMPLE file=\"nowhere.wav\"/></VOICE></BATIDA>",
                   "sample not found");
            check ("setting.batida-kit", "<BATIDA type=\"kit\"><KIT seq_tempo=\"140\"/></BATIDA>", "ignored in a kit file");
            check ("index.batida-kit", "<BATIDA type=\"kit\"><KIT><VOICE index=\"8\"/></KIT></BATIDA>", "index must be 0..7");
            check ("target.batida-kit", "<BATIDA type=\"kit\"><KIT><MOVEMENT><MOD index=\"0\"><TARGET global=\"xy_z\"/></MOD></MOVEMENT></KIT></BATIDA>",
                   "did you mean xy_x");
            check ("gate.batida-pattern", "<BATIDA type=\"pattern\"><PATTERN><TRACK voice=\"0\" gate=\"x..y\"/></PATTERN></BATIDA>", "for a hit");
            check ("velocity.batida-pattern", "<BATIDA type=\"pattern\"><PATTERN><TRACK voice=\"0\" velocity=\"100,200\"/></PATTERN></BATIDA>",
                   "step 2");
            check ("track.batida-pattern", "<BATIDA type=\"pattern\"><PATTERN><TRACK gate=\"x\"/></PATTERN></BATIDA>", "voice must be 0..7");
            check ("length.batida-pattern", "<BATIDA type=\"pattern\"><PATTERN length=\"100\"/></BATIDA>", "8..64");
            check ("xy.batida-pattern", "<BATIDA type=\"pattern\"><PATTERN xy=\"-,0.5\"/></BATIDA>", "x:y");
            check ("type.batida-kit", "<BATIDA type=\"sound\"><VOICE/></BATIDA>", "must be \"kit\"", true);
            check ("broken.batida-kit", "<BATIDA type=\"kit\"><KIT></BATIDA>", "not readable XML", true);
            check ("other.batida-kit", "<PRESET/>", "root element", true);
            check ("sound.xml", soundFile (""), "not a Batida file", true);
            expectEquals (nearestKey ("amp_deacy", { "amp_decay", "amp_attack" }), juce::String ("amp_decay"));
            expectEquals (nearestKey ("completely_else", { "amp_decay" }), juce::String());
        }

        beginTest ("Every factory file validates clean");
        {
            int files = 0;
            for (const auto& e : juce::RangedDirectoryIterator (juce::File (BATIDA_SOURCE_DIR).getChildFile ("resources/factory"), true,
                                                                "*.batida-*", juce::File::findFiles))
            {
                const auto v = validateFile (e.getFile());
                expect (v.clean(), e.getFile().getFileName() + ": " + problemsOf (v));
                ++files;
            }
            expect (files > 100, "found the factory: " + juce::String (files));
        }

        beginTest ("Measurements of known signals");
        {
            // A 1 kHz sine at -20 dBFS: -23 LUFS in one channel (BS.1770), at
            // 48 kHz and, with derived filters, at 44.1 kHz.
            for (const auto rate : { 48000.0, 44100.0, 96000.0 })
            {
                const auto m = measureAudio (sine (1000.0f, 0.1f, rate, 2.0), rate);
                expectWithinAbsoluteError (m.loudnessLufs, -23.0f, 0.3f, "LUFS at " + juce::String (rate));
                expectWithinAbsoluteError (m.peakDb, -20.0f, 0.05f);
            }
            const auto a = measureAudio (sine (440.0f, 0.5f, kRate, 1.0), kRate);
            expectWithinAbsoluteError (a.peakDb, -6.02f, 0.05f);
            expectWithinAbsoluteError (a.sound.pitchHz, 440.0f, 4.0f);
            expectEquals (noteNumberFor (a.sound.pitchHz), 69);
            expectEquals (noteName (69), juce::String ("A3"));
            expectEquals (noteName (36), juce::String ("C1"));
            expect (a.sound.flatness < 0.1f, "a sine is tonal");
            expect (a.clipped == 0 && a.finite && ! a.isSilent());
            expectEquals (a.bandsDb.size(), (size_t) 8);
            expect (a.bandsDb[3] > -3.0f, "440 Hz sits in the 500 Hz octave");

            juce::AudioBuffer<float> noise (1, (int) kRate);
            juce::Random r (5);
            for (int i = 0; i < noise.getNumSamples(); ++i)
                noise.setSample (0, i, 0.5f * (r.nextFloat() * 2.0f - 1.0f));
            expect (measureAudio (noise, kRate).sound.flatness > 0.5f, "noise is flat");

            // A 12 kHz sine sampled 45 degrees off its peaks: the true peak is 3 dB over the samples.
            const auto tp = measureAudio (sine (12000.0f, 0.5f, kRate, 0.5, 1, juce::MathConstants<float>::pi / 4.0f), kRate);
            expectWithinAbsoluteError (tp.truePeakDb - tp.peakDb, 3.0f, 0.5f);

            auto stereo = sine (200.0f, 0.5f, kRate, 0.5, 2);
            expectWithinAbsoluteError (measureAudio (stereo, kRate).widthPercent, 0.0f, 0.01f, "mono in stereo");
            juce::FloatVectorOperations::negate (stereo.getWritePointer (1), stereo.getReadPointer (1), stereo.getNumSamples());
            expectWithinAbsoluteError (measureAudio (stereo, kRate).widthPercent, 100.0f, 0.01f, "out of phase");

            // Three clicks: three onsets.
            juce::AudioBuffer<float> clicks (1, (int) kRate);
            clicks.clear();
            for (const auto at : { 0.1, 0.4, 0.7 })
                for (int i = 0; i < 2000; ++i)
                    clicks.setSample (0, (int) (at * kRate) + i, 0.8f * std::exp ((float) -i / 300.0f) * (r.nextFloat() * 2.0f - 1.0f));
            const auto on = measureAudio (clicks, kRate).onsets;
            expectEquals ((int) on.size(), 3);
            if (on.size() == 3)
                expectWithinAbsoluteError (on[1].seconds, 0.4, 0.01);

            juce::AudioBuffer<float> silence (2, 4800);
            silence.clear();
            expect (measureAudio (silence, kRate).isSilent());
        }

        beginTest ("Renders match the engine's own");
        {
            SetPreset set;
            set.info.name = "Beat";
            set.kit = defaultKitPreset();
            set.kit.globals[gp::SeqTempo] = 126.0f;
            set.patterns.patterns[0] = breakbeatPattern();
            const auto f = scratch().getChildFile ("render/Beat.batida-set");
            expect (writeSet (f, set));
            const auto wav = scratch().getChildFile ("render/beat.wav");
            const auto r = batidaCli ({ "render", f.getFullPathName(), "--loop", "-o", wav.getFullPathName(), "--json" });
            expectEquals (r.code, 0, r.err);
            expectEquals ((double) r.json()["bpm"], 126.0);

            RenderRequest req;
            req.mode = RenderRequest::Mode::Pattern;
            req.params.voices = set.kit.voices;
            req.params.global = set.kit.globals;
            req.movement = set.kit.movement;
            req.bank = set.patterns;
            req.bpm = 126.0;
            const auto direct = renderKit (req);

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (wav));
            expect (reader != nullptr);
            if (reader != nullptr)
            {
                expectEquals ((int) reader->lengthInSamples, direct.getNumSamples());
                juce::AudioBuffer<float> read (2, (int) reader->lengthInSamples);
                reader->read (&read, 0, read.getNumSamples(), 0, true, true);
                float worst = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < read.getNumSamples(); ++i)
                        worst = std::max (worst, std::abs (read.getSample (ch, i) - direct.getSample (ch, i)));
                expect (worst < 1.0e-5f, "the same audio (24-bit): " + juce::String (worst));
            }

            // Two bars from the start, with the tails.
            const auto bars = batidaCli ({ "render", f.getFullPathName(), "--bars", "2", "-o", wav.getFullPathName(), "--json" });
            expectEquals (bars.code, 0);
            const auto seconds = (double) bars.json()["measures"]["seconds"];
            const auto twoBars = 2 * 4 * 60.0 / 126.0;
            expect (seconds > twoBars && seconds < twoBars + 6.1, "two bars and a tail: " + juce::String (seconds));

            // One hit, at another note.
            const auto hit = batidaCli ({ "render", f.getFullPathName(), "--sound", "1", "--note", "48", "-o", wav.getFullPathName(), "--json" });
            expectEquals (hit.code, 0);
            expect ((double) hit.json()["measures"]["seconds"] < 6.1);
        }

        beginTest ("The batida tool");
        {
            expectEquals (batidaCli ({}).code, 0);
            expect (batidaCli ({ "help" }).out.contains ("render"));
            expectEquals (batidaCli ({ "render", "--help" }).code, 0);
            expect (batidaCli ({ "help", "render" }).out.contains ("--bars"));
            expectEquals (batidaCli ({ "frobnicate" }).code, 2);
            expectEquals (batidaCli ({ "render" }).code, 2);
            expectEquals (batidaCli ({ "render", "nowhere.batida-kit" }).code, 2);
            expectEquals (batidaCli ({ "render", scratch().getChildFile ("examples/Example.batida-kit").getFullPathName() }).code, 2,
                          "a kit needs --sound or --pattern-file");
            expectEquals (batidaCli ({ "validate", "--wat", "x" }).code, 2);
            expect (batidaCli ({ "--version" }).out.startsWith ("batida "));

            // validate: 0 clean, 1 with problems; JSON lists them.
            const auto good = scratch().getChildFile ("examples/Example.batida-kit").getFullPathName();
            const auto bad = writeText ("cli/Bad.batida-sound", soundFile ("flt_cuttoff=\"800\"")).getFullPathName();
            expectEquals (batidaCli ({ "validate", good }).code, 0);
            const auto v = batidaCli ({ "validate", good, bad, "--json" });
            expectEquals (v.code, 1);
            expect (v.json().isArray() && v.json().size() == 2);
            expect (v.json()[1]["problems"][0]["problem"].toString().contains ("flt_cutoff"));

            // describe: a pattern as a grid.
            const auto d = batidaCli ({ "describe", scratch().getChildFile ("examples/Example.batida-pattern").getFullPathName() });
            expectEquals (d.code, 0);
            expect (d.out.contains ("x... x... x... x..."), d.out);
            const auto dj = batidaCli ({ "describe", scratch().getChildFile ("examples/Example.batida-kit").getFullPathName(), "--json" });
            expectEquals (dj.json()["slots"][2]["changed"]["amp_decay"].toString(), juce::String ("180 ms"));
            expect (dj.json()["kit"]["xy_y"].toString() == "0.5");

            // new: a file to start from; not over an existing one without --force.
            const auto folder = scratch().getChildFile ("cli/new");
            const auto n = batidaCli ({ "new", "kit", "Fresh", "-o", folder.getFullPathName() });
            expectEquals (n.code, 0, n.err);
            expect (folder.getChildFile ("Fresh.batida-kit").existsAsFile());
            expect (validateFile (folder.getChildFile ("Fresh.batida-kit")).clean());
            expectEquals (batidaCli ({ "new", "kit", "Fresh", "-o", folder.getFullPathName() }).code, 1);
            expectEquals (batidaCli ({ "new", "kit", "Fresh", "-o", folder.getFullPathName(), "--force" }).code, 0);
            expectEquals (batidaCli ({ "new", "drum", "X" }).code, 2);

            // params: every setting.
            const auto p = batidaCli ({ "params", "--json" });
            expectEquals (p.json().size(), kNumVoiceParams + kNumGlobalParams);
            expect (batidaCli ({ "params", "--markdown" }).out == referenceMarkdown());

            // analyze, against another file.
            const auto wav = scratch().getChildFile ("examples/kit.wav").getFullPathName();
            const auto other = scratch().getChildFile ("examples/sound.wav").getFullPathName();
            const auto an = batidaCli ({ "analyze", wav, "--against", other, "--json" });
            expectEquals (an.code, 0, an.err);
            expect (an.json()["difference"].hasProperty ("loudnessLufs"));
            expect (batidaCli ({ "analyze", wav }).out.contains ("LUFS"));
            expectEquals (batidaCli ({ "analyze", "nowhere.wav" }).code, 2);

            // list: the library in $BATIDA_LIBRARY.
            const auto oldLibrary = juce::SystemStats::getEnvironmentVariable ("BATIDA_LIBRARY", {});
            const auto lib = scratch().getChildFile ("cli/Library");
            setenv ("BATIDA_LIBRARY", lib.getFullPathName().toRawUTF8(), 1);
            expectEquals (batidaCli ({ "new", "sound", "Thump" }).code, 0);
            expect (lib.getChildFile ("User/Sounds/Other/Thump.batida-sound").existsAsFile());
            const auto l = batidaCli ({ "list", "--json" });
            expectEquals (l.code, 0);
            expect (l.json().size() == 1 && l.json()[0]["name"].toString() == "Thump", l.out);
            expectEquals (batidaCli ({ "list", "--type", "kit", "--json" }).json().size(), 0);
            if (oldLibrary.isNotEmpty())
                setenv ("BATIDA_LIBRARY", oldLibrary.toRawUTF8(), 1);
            else
                unsetenv ("BATIDA_LIBRARY");
        }

        beginTest ("The library notices new, changed and removed files");
        {
            const auto root = scratch().getChildFile ("watch");
            const auto user = root.getChildFile ("User");
            user.createDirectory();
            const auto empty = Library::folderSignature (user);
            const auto f = user.getChildFile ("Kits/A.batida-kit");
            f.getParentDirectory().createDirectory();
            expect (writeKit (f, defaultKitPreset()));
            const auto one = Library::folderSignature (user);
            expect (one != empty, "new");
            f.appendText (" ");
            expect (Library::folderSignature (user) != one, "changed");
            f.deleteFile();
            expectEquals (Library::folderSignature (user), empty, "removed");

            Library lib (root, root.getChildFile ("settings.xml"));
            lib.scanNow();
            expectEquals ((int) lib.getEntries().size(), 0);
            lib.startWatching();
            lib.startWatching(); // two editors
            lib.stopWatching();
            expect (lib.isWatching(), "one editor still watching");
            expect (writeKit (user.getChildFile ("Kits/B.batida-kit"), defaultKitPreset()));
            expect (waitFor ([&] { return lib.getEntries().size() == 1; }, 6000), "picked up without a rescan");
            lib.stopWatching();
            expect (! lib.isWatching());
        }

        beginTest ("A loaded file changed on disk");
        {
            BatidaProcessor proc;
            proc.prepareToPlay (kRate, 256);
            const auto f = scratch().getChildFile ("reload/Kick.batida-sound");
            f.getParentDirectory().createDirectory();
            f.replaceWithText (soundFile ("amp_decay=\"300\""));
            expect (proc.loadPresetFile (f, 2, 0, BatidaProcessor::LoadMode::Step));
            expect (! proc.originChanged (PresetType::Sound, 2, 0));
            expect (! proc.originChanged (PresetType::Sound, 3, 0), "another slot");

            f.replaceWithText (soundFile ("amp_decay=\"900\""));
            f.setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::seconds (2.0));
            expect (proc.originChanged (PresetType::Sound, 2, 0), "newer on disk");

            // The project remembers when it last read the file.
            juce::MemoryBlock state;
            proc.getStateInformation (state);
            {
                BatidaProcessor reopened;
                reopened.setStateInformation (state.getData(), (int) state.getSize());
                expect (reopened.originChanged (PresetType::Sound, 2, 0), "still shows after reopening");
            }

            expect (proc.reloadOrigin (PresetType::Sound, 2, 0));
            expectWithinAbsoluteError (proc.readVoiceParams (2)[vp::AmpD], 900.0f, 0.01f);
            expect (! proc.originChanged (PresetType::Sound, 2, 0), "seen again");
            expect (proc.canUndo(), "one undo step");

            // Batida's own save isn't a change from outside.
            PresetInfo info;
            info.name = "Kick";
            expect (proc.saveSound (2, f, info, false));
            expect (! proc.originChanged (PresetType::Sound, 2, 0));

            // A kit loaded from a set: the kit strip follows the set.
            SetPreset set;
            set.kit = defaultKitPreset();
            const auto sf = scratch().getChildFile ("reload/All.batida-set");
            expect (writeSet (sf, set));
            expect (proc.loadPresetFile (sf, 0, 0, BatidaProcessor::LoadMode::Step));
            expect (proc.originFile (PresetType::Kit, 0, 0) == sf);
            sf.setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::seconds (2.0));
            expect (proc.originChanged (PresetType::Kit, 0, 0));
            proc.releaseResources();
        }
    }
};

static ScriptingTests scriptingTests;

} // namespace batida
