#include "Plugin/PluginProcessor.h"

#include "Engine/ParamRange.h"
#include "Engine/Sequencer/PatternVary.h"

#include <cmath>
#include <cstdlib>
#include <thread>

// The plugin as a host sees it: the processor, its parameters, its saved
// state and the actions the editor calls. Runs on a scratch library.

namespace
{
using namespace batida;

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

juce::File scratchDir()
{
    static const auto dir = []
    {
        auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("BatidaProcessorTests");
        d.deleteRecursively();
        d.createDirectory();
        setenv ("BATIDA_LIBRARY", d.getChildFile ("Library").getFullPathName().toRawUTF8(), 1);
        return d;
    }();
    return dir;
}

juce::File writeTone (const juce::File& file, float hz, int frames = 9600)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::AudioBuffer<float> b (1, frames);
    for (int i = 0; i < frames; ++i)
        b.setSample (0, i, 0.5f * std::sin (6.2831853f * hz * (float) i / (float) kRate));
    std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
    juce::WavAudioFormat wav;
    if (auto w = wav.createWriterFor (out, juce::AudioFormatWriterOptions {}.withSampleRate (kRate).withNumChannels (1).withBitsPerSample (16)))
        w->writeFromAudioSampleBuffer (b, 0, frames);
    return file;
}

void setParam (BatidaProcessor& proc, const juce::String& id, float value)
{
    auto* p = proc.getState().getParameter (id);
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}

float getParam (BatidaProcessor& proc, const juce::String& id)
{
    auto* p = proc.getState().getParameter (id);
    return p->convertFrom0to1 (p->getValue());
}

float render (BatidaProcessor& proc, juce::AudioBuffer<float>& buffer, int blocks)
{
    juce::MidiBuffer midi;
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        proc.processBlock (buffer, midi);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = std::max (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
    }
    return peak;
}
} // namespace

class ProcessorTests final : public juce::UnitTest
{
public:
    ProcessorTests() : juce::UnitTest ("Processor", "Batida") {}

    void runTest() override
    {
        const auto dir = scratchDir();

        beginTest ("Parameter IDs never change or disappear (saved projects and automation use them)");
        {
            // tests/golden/parameter-ids.txt is the list every release shipped with.
            // A new parameter is added to it on purpose; nothing is ever removed.
            // BATIDA_WRITE_GOLDEN=1 rewrites it from the current build.
            BatidaProcessor proc;
            juce::StringArray ids;
            for (auto* p : proc.getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                    ids.add (r->getParameterID());
            const auto golden = juce::File (BATIDA_SOURCE_DIR).getChildFile ("tests/golden/parameter-ids.txt");
            if (juce::SystemStats::getEnvironmentVariable ("BATIDA_WRITE_GOLDEN", {}) == "1")
            {
                golden.getParentDirectory().createDirectory();
                golden.replaceWithText (ids.joinIntoString ("\n") + "\n");
            }
            juce::StringArray known;
            known.addLines (golden.loadFileAsString());
            known.removeEmptyStrings();
            expect (known.size() > 0, "the list exists: " + golden.getFullPathName());
            for (const auto& id : known)
                expect (ids.contains (id), "still there: " + id);
            for (const auto& id : ids)
                expect (known.contains (id), "new, add it to the list on purpose: " + id);
            logMessage ("  " + juce::String (ids.size()) + " parameters");
        }

        beginTest ("Host parameter groups say Sound, IDs still say voice");
        {
            BatidaProcessor proc;
            const auto& groups = proc.getParameterTree().getSubgroups (false);
            expectEquals (groups.size(), kNumVoices);
            for (int v = 0; v < groups.size(); ++v)
            {
                expectEquals (groups[v]->getName(), "Sound " + juce::String (v + 1));
                expectEquals (groups[v]->getID(), "voice" + juce::String (v + 1));
            }
        }

        beginTest ("Typing -inf gives the bottom of the range, not 0 dB");
        {
            BatidaProcessor proc;
            auto* level = proc.getState().getParameter (voiceParamID (0, vp::Level));
            const auto& spec = voiceParamSpecs()[(size_t) vp::Level];
            expectWithinAbsoluteError (level->convertFrom0to1 (level->getValueForText ("-inf dB")), spec.min, 1.0e-3f);
            expectWithinAbsoluteError (level->convertFrom0to1 (level->getValueForText ("-6 dB")), -6.0f, 1.0e-2f);
            expectWithinAbsoluteError (level->convertFrom0to1 (level->getValueForText ("loud")), spec.def, 1.0e-3f,
                                       "no number keeps the default");
            auto* pan = proc.getState().getParameter (voiceParamID (0, vp::Pan));
            expectWithinAbsoluteError (pan->convertFrom0to1 (pan->getValueForText ("C")), 0.0f, 1.0e-3f);
            expectWithinAbsoluteError (pan->convertFrom0to1 (pan->getValueForText ("L50")), -0.5f, 1.0e-3f);
        }

        beginTest ("Dist Type and Wave read back the text they show");
        {
            BatidaProcessor proc;
            auto* dist = proc.getState().getParameter (globalParamID (gp::DistType));
            auto* wave = proc.getState().getParameter (voiceParamID (0, opParam (1, Wave)));
            for (auto* p : { dist, wave })
                for (const auto v : { 0.0f, 0.1f, 0.25f, 0.4f, 0.5f, 0.875f, 1.0f })
                {
                    const auto text = p->getText (v, 64);
                    expectWithinAbsoluteError (p->getValueForText (text), v, 0.003f, text);
                }
            expectWithinAbsoluteError (dist->getValueForText ("tube"), 0.25f, 1.0e-4f);
            expectWithinAbsoluteError (dist->getValueForText ("Crush"), 1.0f, 1.0e-4f);
            expectWithinAbsoluteError (wave->getValueForText ("sq"), 0.75f, 1.0e-4f);
            expectWithinAbsoluteError (wave->getValueForText ("0.3"), 0.3f, 1.0e-4f, "a plain number still works");
            auto* pan = proc.getState().getParameter (voiceParamID (0, vp::Pan));
            expectWithinAbsoluteError (pan->convertFrom0to1 (pan->getValueForText ("-50")), -0.5f, 1.0e-3f, "a bare number: negative is left");
            expectWithinAbsoluteError (pan->convertFrom0to1 (pan->getValueForText ("R25")), 0.25f, 1.0e-3f);
        }

        beginTest ("The tail the host is told covers the longest ring-out");
        {
            BatidaProcessor proc;
            expectGreaterOrEqual (proc.getTailLengthSeconds(), 2.0);
            setParam (proc, voiceParamID (5, vp::AmpD), 8000.0f);
            setParam (proc, voiceParamID (5, vp::AmpR), 8000.0f);
            expectGreaterThan (proc.getTailLengthSeconds(), 21.0, "8 s of decay and 8 s of release, each to -80 dB");
            setParam (proc, voiceParamID (5, vp::AmpD), 200.0f);
            setParam (proc, voiceParamID (5, vp::AmpR), 100.0f);
            expectLessThan (proc.getTailLengthSeconds(), 21.0, "short again with the sound");
        }

        beginTest ("Swapping slots moves Save targets and sample sizes with the sounds");
        {
            BatidaProcessor proc;
            proc.prepareToPlay (kRate, kBlock);
            const auto wav = writeTone (dir.getChildFile ("kickA.wav"), 60.0f);
            SoundPreset kick;
            kick.info.name = "Kick A";
            kick.params = proc.readVoiceParams (0);
            kick.params[vp::SrcMode] = (float) SourceMode::Sample;
            kick.sample = { wav.getFullPathName(), wav.getSize() };
            const auto from = dir.getChildFile ("Kick A.batida-sound");
            proc.applySound (0, kick, from);
            expect (proc.getOrigins().soundFiles[0] == from);

            proc.swapVoices (0, 1);
            expect (proc.getOrigins().soundFiles[1] == from, "slot 2 now saves to Kick A");
            expect (proc.getOrigins().soundFiles[0] == juce::File(), "slot 1 no longer does");
            expectEquals (proc.missingSampleBytes (1), wav.getSize());
            expectEquals (proc.sampleSlot (1).getPath(), wav.getFullPathName());
            expectEquals (proc.getVoiceName (1), juce::String ("Kick A"));

            proc.undo();
            expect (proc.getOrigins().soundFiles[0] == from, "undo puts it back");
        }

        beginTest ("A sample that can't be read leaves the slot silent, never the previous sample");
        {
            BatidaProcessor proc;
            proc.prepareToPlay (kRate, kBlock);
            const auto good = writeTone (dir.getChildFile ("good.wav"), 220.0f);
            expect (proc.loadSample (2, good));
            const auto bad = dir.getChildFile ("bad.wav");
            bad.replaceWithText ("not audio");

            SoundPreset s;
            s.params = proc.readVoiceParams (2);
            s.sample = { bad.getFullPathName(), bad.getSize() };
            proc.applySound (2, s, {});
            auto& slot = proc.sampleSlot (2);
            expect (slot.getStatus() == SampleSlot::Status::Error);
            expect (slot.getDisplayData() == nullptr, "the old sample is gone");
            expectEquals (slot.getPath(), bad.getFullPathName(), "the reference is kept for Save");

            // Dropping an unreadable file straight on a slot leaves it as it was.
            expect (proc.loadSample (3, good));
            expect (! proc.loadSample (3, bad));
            expectEquals (proc.sampleSlot (3).getPath(), good.getFullPathName());
        }

        beginTest ("A shared sample outlives its replacement");
        {
            SampleSlot slot;
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            expect (slot.load (writeTone (dir.getChildFile ("a.wav"), 100.0f, 48000), formats));
            const auto held = slot.share();
            expect (held != nullptr);
            expect (slot.load (writeTone (dir.getChildFile ("b.wav"), 300.0f), formats));
            std::this_thread::sleep_for (std::chrono::milliseconds (1100));
            slot.collectGarbage();
            expectEquals (held->numFrames(), 48000, "Vary's copy is still readable");
            expect (slot.getDisplayData() != held.get());
        }

        beginTest ("Bypass is silent, even with a sidechain coming in");
        {
            BatidaProcessor proc;
            proc.prepareToPlay (kRate, kBlock);
            juce::AudioBuffer<float> buffer (2, kBlock);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlock; ++i)
                    buffer.setSample (ch, i, 0.5f);
            juce::MidiBuffer midi;
            proc.processBlockBypassed (buffer, midi);
            expectEquals (buffer.getMagnitude (0, kBlock) + buffer.getMagnitude (1, kBlock), 0.0f);
        }

        beginTest ("A mono output mixes both sides, whatever the sidechain's width");
        {
            BatidaProcessor proc;
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (juce::AudioChannelSet::stereo());
            layout.outputBuses.add (juce::AudioChannelSet::mono());
            expect (proc.setBusesLayout (layout));
            proc.prepareToPlay (kRate, kBlock);
            setParam (proc, voiceParamID (0, vp::Pan), 1.0f); // hard right
            setParam (proc, voiceParamID (0, vp::ChainAmt), 0.0f);
            juce::AudioBuffer<float> buffer (2, kBlock); // 2 channels: the stereo sidechain in
            buffer.clear();
            proc.audition (0, true);
            const auto peak = render (proc, buffer, 8);
            expectGreaterThan (peak, 0.05f, "the right-panned kick reaches the mono output");
        }

        beginTest ("Clicking a slot in Drum map mode keeps held notes");
        {
            auto sustained = [&] (int midiMode)
            {
                BatidaProcessor proc;
                proc.prepareToPlay (kRate, kBlock);
                setParam (proc, globalParamID (gp::MidiMode), (float) midiMode);
                setParam (proc, voiceParamID (5, vp::PlayMode), (float) PlayMode::Gate);
                setParam (proc, voiceParamID (5, vp::AmpS), 1.0f);
                setParam (proc, voiceParamID (5, vp::AmpR), 5.0f);
                setParam (proc, voiceParamID (5, vp::ChainAmt), 0.0f);
                for (int v = 0; v < kNumVoices; ++v)
                    if (v != 5)
                        setParam (proc, voiceParamID (v, vp::Mute), 1.0f);

                juce::AudioBuffer<float> buffer (2, kBlock);
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (10, kDrumMapFirstNote + 5, (juce::uint8) 100), 0);
                proc.processBlock (buffer, midi);
                render (proc, buffer, 4);
                proc.setKeysVoice (2); // what a click on slot 3 does
                render (proc, buffer, 8);
                return render (proc, buffer, 2);
            };
            expectGreaterThan (sustained (0), 0.01f, "Drum map: the held note keeps sounding");
        }

        beginTest ("An old project without patterns or movement gets the defaults");
        {
            BatidaProcessor source;
            juce::MemoryBlock saved;
            source.getStateInformation (saved);
            auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
            expect (xml != nullptr);
            xml->deleteAllChildElementsWithTagName ("PATTERNS");
            xml->deleteAllChildElementsWithTagName ("MOVEMENT");
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*xml, old);

            BatidaProcessor proc;
            proc.patterns().edit ([] (PatternBank& b) { b.patterns[3].tracks[0].steps[5].gate = true; });
            proc.setStateInformation (old.getData(), (int) old.getSize());
            expect (! proc.patterns().get().patterns[3].tracks[0].steps[5].gate, "the last project's pattern is gone");
        }

        beginTest ("Resample a pattern into a slot, then turn that loop back into a kit");
        {
            BatidaProcessor proc;
            proc.prepareToPlay (kRate, kBlock);
            proc.patterns().edit ([] (PatternBank& b) { b.patterns[0] = breakbeatPattern(); });
            setParam (proc, globalParamID (gp::SeqTempo), 120.0f);

            juce::String error;
            expect (proc.resamplePattern (0, 5, &error), error);
            auto& slot = proc.sampleSlot (5);
            expect (slot.getStatus() == SampleSlot::Status::Loaded);
            const juce::File loop (slot.getPath());
            expect (loop.existsAsFile() && loop.getParentDirectory().getFileName() == "Resampled", loop.getFullPathName());
            expectWithinAbsoluteError ((double) slot.getDisplayData()->numFrames(), 2.0 * kRate, 2.0, "one pass: 16 steps at 120 bpm");
            expectWithinAbsoluteError (getParam (proc, voiceParamID (5, vp::ChainAmt)), 0.0f, 1.0e-4f, "dry: the chain is in the render");
            expectEquals (getParam (proc, voiceParamID (5, vp::SrcMode)), (float) SourceMode::Sample);

            expect (proc.resampleSound (0, 4, &error), error);
            expect (proc.sampleSlot (4).getStatus() == SampleSlot::Status::Loaded);
            expect (! proc.resamplePattern (7, 3, &error), "an empty pattern doesn't render");

            // The loop as a break: the kit's own beat comes back as slices.
            const auto result = proc.loadBreak (loop, 1, &error);
            expect (result.has_value(), error);
            if (result)
            {
                logMessage ("  break: " + juce::String (result->hits) + " hits, " + juce::String (result->steps) + " steps, "
                            + juce::String (result->bpm, 1) + " bpm");
                expectEquals (result->steps, 16);
                expectWithinAbsoluteError (result->bpm, 120.0, 0.5);
                expectGreaterThan (result->hits, 5);
                expect (proc.getVoiceName (0).startsWith ("Break"), proc.getVoiceName (0));
                expectEquals ((int) getParam (proc, voiceParamID (0, vp::SmpSliceMode)), (int) SliceMode::Transients);
                expect (! proc.patterns().get().patterns[1].isEmpty());
                expect (proc.sampleSlot (0).getDisplayData() == proc.sampleSlot (2).getDisplayData(), "one loop shared");
                proc.undo();
                expect (proc.patterns().get().patterns[1].isEmpty(), "one undo takes it all back");
                expect (! proc.getVoiceName (0).startsWith ("Break"));
            }
        }

        beginTest ("A pattern exports as a MIDI file in the library");
        {
            BatidaProcessor proc;
            proc.patterns().edit ([] (PatternBank& b) { b.patterns[2] = breakbeatPattern(); });
            juce::String error;
            const auto file = proc.exportPatternMidi (2, &error);
            expect (file.existsAsFile(), error);
            expect (file.getFileName().startsWith ("P03"), file.getFileName());
            juce::FileInputStream in (file);
            juce::MidiFile midi;
            expect (midi.readFrom (in));
            expectEquals (midi.getNumTracks(), 1);
            int notes = 0;
            for (const auto* e : *midi.getTrack (0))
                notes += e->message.isNoteOn() ? 1 : 0;
            expectGreaterThan (notes, 8);
            expect (proc.exportPatternMidi (2) == file, "exporting again replaces the file");
        }

        beginTest ("Pattern Vary: preview leaves the pattern alone, KEEP is one undo step");
        {
            BatidaProcessor proc;
            proc.patterns().edit ([] (PatternBank& b) { b.patterns[0] = breakbeatPattern(); });
            const auto original = proc.patterns().get().patterns[0];
            proc.startPatternVary (0, 0.5f, PatternDirection::Any, {});
            expectEquals ((int) proc.patternCandidates().size(), 4);
            proc.previewPatternCandidate (1);
            expectEquals (patternDistance (proc.patterns().get().patterns[0], original), 0, "previewing doesn't edit");
            const auto chosen = proc.patternCandidates()[1].pattern;
            proc.keepPatternCandidate();
            expectEquals (patternDistance (proc.patterns().get().patterns[0], chosen), 0);
            proc.undo();
            expectEquals (patternDistance (proc.patterns().get().patterns[0], original), 0);
        }

        beginTest ("Loads, swaps and undo drop any Vary preview");
        {
            BatidaProcessor proc;
            proc.patterns().edit ([] (PatternBank& b) { b.patterns[0] = breakbeatPattern(); });
            proc.startPatternVary (0, 0.5f, PatternDirection::Any, {});
            proc.previewPatternCandidate (0);
            expect (proc.getKit().getSequencer().isRunning() == false);
            proc.swapVoices (0, 1);
            expectEquals (proc.previewedPatternCandidate(), -1, "a swap ends the preview");
            expect (proc.patternCandidates().empty(), "and drops suggestions made for the old slots");

            proc.startPatternVary (0, 0.5f, PatternDirection::Any, {});
            proc.previewPatternCandidate (1);
            proc.initSound (3);
            expectEquals (proc.previewedPatternCandidate(), -1, "a load ends it");

            proc.startPatternVary (0, 0.5f, PatternDirection::Any, {});
            proc.previewPatternCandidate (2);
            proc.undo();
            expectEquals (proc.previewedPatternCandidate(), -1, "undo ends it");
        }

        beginTest ("Kit Vary: whole-kit suggestions, locked slots untouched, KEEP undoable");
        {
            BatidaProcessor proc;
            proc.prepareToPlay (kRate, kBlock);
            std::array<bool, kNumVoices> locked {};
            locked[0] = true;
            const auto before = proc.readVoiceParams (0);
            const auto snareBefore = proc.readVoiceParams (2);
            proc.startKitVary (0.4f, VaryDirection::Darker, false, false, false, locked);
            const auto until = juce::Time::getMillisecondCounter() + 60000;
            while (proc.isKitVarying() && juce::Time::getMillisecondCounter() < until)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            expect (! proc.isKitVarying());
            expectGreaterThan ((int) proc.kitCandidates().size(), 0);
            for (const auto& c : proc.kitCandidates())
                expect ((c.changed & 1u) == 0, "the locked kick never changes");
            proc.previewKitCandidate (0);
            expect (proc.previewedKitCandidate() == 0);
            proc.keepKitCandidate();
            expect (proc.readVoiceParams (0)[vp::FmBright] == before[vp::FmBright]);
            bool changed = false;
            for (int k = 0; k < kNumVoiceParams; ++k)
                changed = changed || std::abs (proc.readVoiceParams (2)[k] - snareBefore[k]) > 1.0e-4f;
            expect (changed || (proc.kitCandidates().empty()), "kept");
            proc.undo();
            bool back = true;
            for (int k = 0; k < kNumVoiceParams; ++k)
                back = back && std::abs (proc.readVoiceParams (2)[k] - snareBefore[k]) < 1.0e-3f;
            expect (back, "undo restores the kit");
        }

        beginTest ("Projects from before choke groups load with none");
        {
            BatidaProcessor source;
            juce::MemoryBlock saved;
            source.getStateInformation (saved);
            auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
            for (int v = 0; v < kNumVoices; ++v)
                if (auto* e = xml->getChildByAttribute ("id", voiceParamID (v, vp::Choke)))
                    xml->removeChildElement (e, true);
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*xml, old);

            BatidaProcessor proc;
            expectEquals ((int) getParam (proc, voiceParamID (6, vp::Choke)), 1, "a new Batida's hats choke");
            proc.setStateInformation (old.getData(), (int) old.getSize());
            expectEquals ((int) getParam (proc, voiceParamID (6, vp::Choke)), 0);
            expectEquals ((int) getParam (proc, voiceParamID (7, vp::Choke)), 0);

            BatidaProcessor current;
            current.setStateInformation (saved.getData(), (int) saved.getSize());
            expectEquals ((int) getParam (current, voiceParamID (6, vp::Choke)), 1, "a 0.8 project keeps its groups");
        }

        beginTest ("Vary suggestions never outlive the sound they came from");
        {
            BatidaProcessor proc;
            proc.prepareToPlay (kRate, kBlock);
            auto waitVary = [&]
            {
                const auto until = juce::Time::getMillisecondCounter() + 60000;
                while ((proc.isVarying() || proc.isKitVarying()) && juce::Time::getMillisecondCounter() < until)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            };
            proc.startVary (2, 0.4f, VaryDirection::None, false, false, false);
            waitVary();
            expect (! proc.varyCandidates().empty());
            proc.initSound (2);
            expect (proc.varyCandidates().empty(), "a load drops them");
            expectEquals (proc.varyVoice(), -1);

            proc.startKitVary (0.4f, VaryDirection::None, false, false, false, {});
            proc.initKit(); // while it runs
            waitVary();
            expect (! proc.hasKitVary() && proc.kitCandidates().empty(), "a result that arrives after a load is dropped");

            proc.startKitVary (0.4f, VaryDirection::None, false, false, false, {});
            waitVary();
            expect (proc.hasKitVary());
            const auto results = proc.getVaryResultsVersion();
            proc.previewKitCandidate (0);
            proc.previewKitCandidate (1);
            expectEquals (proc.getVaryResultsVersion(), results, "pressing a tile changes nothing to re-render");
        }

        beginTest ("Swaps share samples in memory; a break keeps one copy through undo and redo");
        {
            BatidaProcessor proc;
            proc.prepareToPlay (kRate, kBlock);
            const auto gone = writeTone (dir.getChildFile ("gone.wav"), 330.0f);
            expect (proc.loadSample (1, gone));
            gone.deleteFile();
            proc.swapVoices (1, 4);
            expect (proc.sampleSlot (4).getStatus() == SampleSlot::Status::Loaded, "still playing though its file is gone");

            proc.patterns().edit ([] (PatternBank& b) { b.patterns[0] = breakbeatPattern(); });
            setParam (proc, globalParamID (gp::SeqTempo), 120.0f);
            juce::String error;
            expect (proc.resamplePattern (0, 5, &error), error);
            const juce::File loop (proc.sampleSlot (5).getPath());
            expect (proc.loadBreak (loop, 1, &error).has_value(), error);
            proc.undo();
            proc.redo();
            const auto* kick = proc.sampleSlot (0).getDisplayData();
            expect (kick != nullptr && kick == proc.sampleSlot (2).getDisplayData() && kick == proc.sampleSlot (5).getDisplayData(),
                    "after undo and redo, one copy for the break's slots (and the loop's own slot)");

            juce::MemoryBlock saved;
            proc.getStateInformation (saved);
            BatidaProcessor reopened;
            reopened.setStateInformation (saved.getData(), (int) saved.getSize());
            const auto* k2 = reopened.sampleSlot (0).getDisplayData();
            expect (k2 != nullptr && k2 == reopened.sampleSlot (2).getDisplayData(), "and when the project opens again");
        }

        beginTest ("Missing samples are found again in the library, by name and size");
        {
            const auto libraryRoot = juce::File (juce::SystemStats::getEnvironmentVariable ("BATIDA_LIBRARY", {}));
            const auto away = writeTone (dir.getChildFile ("elsewhere/moved-snare.wav"), 200.0f);
            juce::MemoryBlock saved;
            {
                BatidaProcessor a;
                a.prepareToPlay (kRate, kBlock);
                expect (a.loadSample (2, away));
                a.getStateInformation (saved);
            }
            // The file moves into the library (a search folder), under the same name.
            const auto moved = libraryRoot.getChildFile ("Moved/moved-snare.wav");
            moved.getParentDirectory().createDirectory();
            expect (away.moveFileTo (moved));

            BatidaProcessor b;
            b.setStateInformation (saved.getData(), (int) saved.getSize());
            expect (b.sampleSlot (2).getStatus() == SampleSlot::Status::Loaded, "relinked when the project opens");
            expectEquals (b.sampleSlot (2).getPath(), moved.getFullPathName());

            // Gone for good (renamed where nothing searches): missing, path kept,
            // and one relink by hand is one undo step.
            const auto renamed = dir.getChildFile ("hidden/renamed-snare.wav");
            renamed.getParentDirectory().createDirectory();
            expect (moved.moveFileTo (renamed));
            BatidaProcessor c;
            c.setStateInformation (saved.getData(), (int) saved.getSize());
            expect (c.sampleSlot (2).getStatus() == SampleSlot::Status::Missing);
            expectEquals (c.sampleSlot (2).getPath(), away.getFullPathName());
            expectEquals (c.numMissingSamples(), 1);
            expect (c.relinkSample (2, renamed));
            expect (c.sampleSlot (2).getStatus() == SampleSlot::Status::Loaded);
            c.undo();
            expect (c.sampleSlot (2).getStatus() == SampleSlot::Status::Missing, "undo takes the relink back");
        }

        beginTest ("Projects saved by earlier releases still open");
        {
            // Written by the tagged release itself (tests/fixtures/README.md).
            // 0.6.1 wrote a byte-identical state, so 0.7.2's stands for both.
            for (const auto* version : { "0.7.2" })
            {
                const auto file = juce::File (BATIDA_SOURCE_DIR).getChildFile ("tests/fixtures/project-" + juce::String (version) + ".batida-state");
                juce::MemoryBlock data;
                expect (file.loadFileAsData (data), "fixture " + file.getFileName());
                BatidaProcessor proc;
                proc.setStateInformation (data.getData(), (int) data.getSize());
                const auto tag = juce::String (" (") + version + ")";
                expectWithinAbsoluteError (getParam (proc, voiceParamID (0, vp::Level)), -7.5f, 0.01f, "level" + tag);
                expectWithinAbsoluteError (getParam (proc, globalParamID (gp::XyY)), 0.6f, 0.001f, "pad" + tag);
                expectWithinAbsoluteError (getParam (proc, voiceParamID (7, vp::AmpD)), 900.0f, 0.5f, "decay" + tag);
                expectEquals (proc.getVoiceName (2), juce::String ("Crack"), "name" + tag);
                const auto& pat = proc.patterns().get().patterns[2];
                expect (pat.tracks[4].steps[9].gate && pat.length == 24, "pattern" + tag);
                expect (proc.sampleSlot (3).getStatus() == SampleSlot::Status::Missing, "a gone sample is missing" + tag);
                expectEquals (proc.sampleSlot (3).getPath(), juce::String ("/tmp/batida-fixture/old-tone.wav"), "its path is kept" + tag);
                for (int v = 0; v < kNumVoices; ++v)
                    expectEquals ((int) getParam (proc, voiceParamID (v, vp::Choke)), 0, "no choke groups" + tag);
            }
        }

        beginTest ("Saved state round-trips parameters, names, samples and patterns");
        {
            BatidaProcessor a;
            a.prepareToPlay (kRate, kBlock);
            const auto wav = writeTone (dir.getChildFile ("snare.wav"), 180.0f);
            expect (a.loadSample (4, wav));
            a.setVoiceName (4, "Crack");
            setParam (a, voiceParamID (4, vp::Level), -7.5f);
            setParam (a, globalParamID (gp::XyY), 0.6f);
            a.patterns().edit ([] (PatternBank& b) { b.patterns[2].tracks[4].steps[9].gate = true; });
            juce::MemoryBlock saved;
            a.getStateInformation (saved);

            BatidaProcessor b;
            b.setStateInformation (saved.getData(), (int) saved.getSize());
            expectEquals (b.getVoiceName (4), juce::String ("Crack"));
            expectWithinAbsoluteError (getParam (b, voiceParamID (4, vp::Level)), -7.5f, 0.01f);
            expectWithinAbsoluteError (getParam (b, globalParamID (gp::XyY)), 0.6f, 0.001f);
            expectEquals (b.sampleSlot (4).getPath(), wav.getFullPathName());
            expect (b.patterns().get().patterns[2].tracks[4].steps[9].gate);

            // A host that restores from another thread: applied on the message
            // thread, and until then saving returns the state as it came.
            BatidaProcessor c;
            std::thread ([&] { c.setStateInformation (saved.getData(), (int) saved.getSize()); }).join();
            expect (c.getVoiceName (4) != juce::String ("Crack"), "nothing changes from the other thread");
            juce::MemoryBlock meanwhile;
            c.getStateInformation (meanwhile);
            expect (meanwhile == saved, "saved meanwhile: the state as it came");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            expectEquals (c.getVoiceName (4), juce::String ("Crack"));
            expectWithinAbsoluteError (getParam (c, voiceParamID (4, vp::Level)), -7.5f, 0.01f);
            expectEquals (c.sampleSlot (4).getPath(), wav.getFullPathName());
            expect (c.patterns().get().patterns[2].tracks[4].steps[9].gate);
        }
    }
};

static ProcessorTests processorTests;
