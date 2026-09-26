#include "PatternMidi.h"

#include "Engine/Kit.h"
#include "Engine/MidiRouter.h"
#include "Sequencer.h"

#include <cmath>

namespace batida
{

juce::MidiFile patternToMidi (const PatternBank& bank, int pattern, const MidiExportOptions& options)
{
    pattern = std::clamp (pattern, 0, kNumPatterns - 1);
    const auto& pat = bank.patterns[(size_t) pattern];

    // Run the real sequencer offline, following a "host" from the song start,
    // so swing, polymeter, ratchets and probability come out exactly as heard.
    constexpr double rate = 48000.0, bpm = 120.0;
    constexpr int block = 512;
    const auto ppqPerSample = bpm / 60.0 / rate;
    const auto passBeats = pat.length * Sequencer::kStep;

    Sequencer seq;
    seq.prepare (rate);
    SeqSettings settings;
    settings.run = RunMode::Transport;
    settings.sync = true;
    settings.pattern = pattern;
    settings.swing = options.swing;

    juce::MidiMessageSequence track;
    auto tick = [] (double beats) { return std::round (beats * kMidiTicksPerBeat); };
    auto cc = [] (float v) { return juce::jlimit (0, 127, (int) std::lround (v * 127.0f)); };
    std::array<double, kNumTracks> openSince {};
    openSince.fill (-1.0);

    std::vector<SeqEvent> events;
    events.reserve (1024);
    for (double start = 0.0; start < passBeats + 0.5; start += block * ppqPerSample)
    {
        Transport t;
        t.hostPlaying = true;
        t.ppq = start;
        t.bpm = bpm;
        events.clear();
        seq.generate (block, t, settings, &bank, events);

        for (const auto& e : events)
        {
            const auto at = start + e.offset * ppqPerSample;
            const auto note = kDrumMapFirstNote + e.voice;
            switch (e.type)
            {
                case SeqEvent::Type::NoteOn:
                    if (at >= passBeats - 1.0e-9)
                        break; // the next pass
                    track.addEvent (juce::MidiMessage::noteOn (kMidiDrumChannel, note, (juce::uint8) juce::jlimit (1, 127, (int) std::lround (e.velocity * 127.0f))),
                                    tick (at));
                    openSince[(size_t) e.voice] = at;
                    break;
                case SeqEvent::Type::NoteOff:
                    if (openSince[(size_t) e.voice] < 0.0)
                        break;
                    track.addEvent (juce::MidiMessage::noteOff (kMidiDrumChannel, note), tick (std::min (at, passBeats)));
                    openSince[(size_t) e.voice] = -1.0;
                    break;
                case SeqEvent::Type::XyLock:
                    if (at >= passBeats - 1.0e-9)
                        break;
                    track.addEvent (juce::MidiMessage::controllerEvent (kMidiDrumChannel, kXyCcX, cc (e.x)), tick (at));
                    track.addEvent (juce::MidiMessage::controllerEvent (kMidiDrumChannel, kXyCcY, cc (e.y)), tick (at));
                    break;
                case SeqEvent::Type::XyRelease:
                    if (at >= passBeats - 1.0e-9)
                        break;
                    track.addEvent (juce::MidiMessage::controllerEvent (kMidiDrumChannel, kXyCcX, cc (options.padX)), tick (at));
                    track.addEvent (juce::MidiMessage::controllerEvent (kMidiDrumChannel, kXyCcY, cc (options.padY)), tick (at));
                    break;
                case SeqEvent::Type::PatternStart:
                    break;
            }
        }
    }

    // Notes still open at the end of the pass close there.
    for (int v = 0; v < kNumTracks; ++v)
        if (openSince[(size_t) v] >= 0.0)
            track.addEvent (juce::MidiMessage::noteOff (kMidiDrumChannel, kDrumMapFirstNote + v), tick (passBeats));

    track.updateMatchedPairs();
    track.addEvent (juce::MidiMessage::endOfTrack(), tick (passBeats));
    track.sort();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (kMidiTicksPerBeat);
    file.addTrack (track);
    return file;
}

} // namespace batida
