#include "Plugin/PluginProcessor.h"

#include "Engine/ParamRange.h"

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
        }
    }
};

static ProcessorTests processorTests;
