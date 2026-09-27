#include "Tools.h"

#include <cmath>

namespace batida::tools
{

juce::AudioBuffer<float> renderVoice (const KitParams& params, int voice, int key, float velocity,
                                      double seconds, double sampleRate, double noteLengthSeconds)
{
    constexpr int block = 128;
    Kit kit;
    // The release movement (no modulation targets), in demo builds too: the
    // demo's Mod 1 moving the pad would change what's measured.
    auto movement = defaultMovement();
    for (auto& mod : movement.mods)
        for (auto& t : mod.targets)
            t = {};
    kit.movementStore().replace (movement);
    kit.setParameters (params); // so the chain starts at these settings, not sweeping to them
    kit.prepare (sampleRate, block);

    const auto total = (int) (seconds * sampleRate);
    const auto offAt = (int) (noteLengthSeconds * sampleRate);
    juce::AudioBuffer<float> out (2, total);
    juce::AudioBuffer<float> buf (2, block);

    for (int pos = 0; pos < total; pos += block)
    {
        const auto n = std::min (block, total - pos);
        buf.setSize (2, n, false, false, true);
        kit.setParameters (params);

        juce::MidiBuffer midi;
        if (pos == 0)
            kit.noteOn (voice, key, velocity);
        if (offAt >= pos && offAt < pos + n)
            kit.noteOff (voice, key);

        kit.process (buf, midi);
        out.copyFrom (0, pos, buf, 0, 0, n);
        out.copyFrom (1, pos, buf, 1, 0, n);
    }
    return out;
}

Stats measure (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    Stats s;
    const auto n = audio.getNumSamples();
    const auto* l = audio.getReadPointer (0);

    double sumSq = 0.0;
    float peak = 0.0f;
    int crossings = 0;

    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            const auto x = audio.getSample (ch, i);
            if (! std::isfinite (x))
                s.finite = false;
            peak = std::max (peak, std::abs (x));
        }
        sumSq += (double) l[i] * l[i];
        if (i > 0 && (l[i - 1] < 0.0f) != (l[i] < 0.0f))
            ++crossings;
    }

    if (peak <= 0.0f)
        return s;

    const auto floor = peak * 0.001f;
    int last = 0;
    for (int i = 0; i < n; ++i)
        if (std::abs (l[i]) > floor)
            last = i;

    s.peakDb = 20.0f * std::log10 (peak);
    s.lengthMs = (float) (last * 1000.0 / sampleRate);
    const auto active = std::max (1, last);
    s.rmsDb = (float) (10.0 * std::log10 (std::max (1.0e-12, sumSq / active)));
    s.centroidHz = (float) (crossings * 0.5 * sampleRate / active);
    return s;
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream,
                                       juce::AudioFormatWriterOptions {}
                                           .withSampleRate (sampleRate)
                                           .withNumChannels (audio.getNumChannels())
                                           .withBitsPerSample (24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

int runRender (const juce::File& outDir)
{
    outDir.createDirectory();
    const auto kit = defaultKitParams();

    std::printf ("%-12s %8s %8s %9s %11s\n", "voice", "peak dB", "rms dB", "length ms", "brightness");
    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto audio = renderVoice (kit, v, 60, 0.85f, 2.0, 48000.0, 0.25);
        const auto s = measure (audio, 48000.0);
        std::printf ("%d %-10s %8.1f %8.1f %9.0f %9.0f Hz%s\n", v + 1, defaultVoiceName (v), s.peakDb, s.rmsDb,
                     s.lengthMs, s.centroidHz, s.finite ? "" : "  NON-FINITE");

        const auto name = juce::String (v + 1) + "-" + juce::String (defaultVoiceName (v)).replace (" ", "") + ".wav";
        writeWav (outDir.getChildFile (name), audio, 48000.0);
    }
    std::printf ("WAVs written to %s\n", outDir.getFullPathName().toRawUTF8());
    return 0;
}

std::pair<float, float> levelOfTail (const juce::AudioBuffer<float>& audio, double seconds, double sampleRate)
{
    const auto start = std::max (0, audio.getNumSamples() - (int) (seconds * sampleRate));
    double sum = 0.0;
    float peak = 0.0f;
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        for (int i = start; i < audio.getNumSamples(); ++i)
        {
            const auto v = audio.getSample (ch, i);
            sum += (double) v * v;
            peak = std::max (peak, std::abs (v));
        }
    const auto n = (double) audio.getNumChannels() * (audio.getNumSamples() - start);
    return { (float) (10.0 * std::log10 (std::max (1.0e-20, sum / n))), 20.0f * std::log10 (std::max (peak, 1.0e-10f)) };
}

juce::AudioBuffer<float> renderLoop (const KitParams& params, int repeats, double sampleRate)
{
    constexpr int block = 128;
    const auto sixteenth = sampleRate * 0.125; // 120 bpm
    const auto total = (int) (sixteenth * 32 * repeats + sampleRate * 0.5);

    // (step, voice) pairs over two bars of 16 steps.
    std::vector<std::pair<int, int>> hits;
    for (int s = 0; s < 32 * repeats; ++s)
    {
        if (s % 4 == 0) hits.push_back ({ s, 0 });                  // kick on the beat
        if (s % 8 == 4) hits.push_back ({ s, 2 });                  // snare on 2 and 4
        if (s % 2 == 0 && s % 16 != 14) hits.push_back ({ s, 6 });  // closed hat on 8ths
        if (s % 16 == 14) hits.push_back ({ s, 7 });                // open hat before the bar
        if (s % 32 == 30) hits.push_back ({ s, 3 });                // clap at the end
    }

    Kit kit;
    kit.setParameters (params);
    kit.prepare (sampleRate, block);
    juce::AudioBuffer<float> out (2, total), buf (2, block);

    for (int pos = 0; pos < total; pos += block)
    {
        const auto n = std::min (block, total - pos);
        buf.setSize (2, n, false, false, true);
        juce::MidiBuffer midi;
        for (const auto& [step, voice] : hits)
        {
            const auto at = (int) std::lround (step * sixteenth);
            if (at >= pos && at < pos + n)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, kDrumMapFirstNote + voice, 0.85f), at - pos);
                midi.addEvent (juce::MidiMessage::noteOff (1, kDrumMapFirstNote + voice), std::min (n - 1, at - pos + 1));
            }
        }
        kit.setParameters (params);
        kit.process (buf, midi);
        out.copyFrom (0, pos, buf, 0, 0, n);
        out.copyFrom (1, pos, buf, 1, 0, n);
    }
    return out;
}

int runPad (const juce::File& outDir)
{
    // Loudness of the beat at five pad positions, relative to bottom-left
    // (warm, clean), plus WAVs for listening.
    outDir.createDirectory();
    struct Position { float x, y; const char* name; };
    const Position positions[] = { { 0.0f, 0.0f, "warm-clean" }, { 1.0f, 0.0f, "digital-clean" },
                                   { 0.5f, 0.5f, "centre" }, { 0.0f, 1.0f, "warm-destroyed" },
                                   { 1.0f, 1.0f, "digital-destroyed" } };

    float reference = 0.0f;
    std::printf ("%-18s %8s %8s %10s\n", "pad position", "rms dB", "peak dB", "vs bottom-left");
    for (const auto& p : positions)
    {
        auto params = defaultKitParams();
        params.global[gp::XyX] = p.x;
        params.global[gp::XyY] = p.y;
        // Three passes; measure the last one, once the adaptive stages have settled.
        const auto audio = renderLoop (params, 3);
        const auto [rms, peak] = levelOfTail (audio, 4.0);
        if (&p == &positions[0])
            reference = rms;
        std::printf ("%-18s %8.1f %8.1f %+10.1f\n", p.name, rms, peak, rms - reference);
        writeWav (outDir.getChildFile (juce::String ("pad-") + p.name + ".wav"), audio, 48000.0);
    }
    std::printf ("WAVs written to %s\n", outDir.getFullPathName().toRawUTF8());
    return 0;
}

int runBeat (const juce::File& outDir)
{
    // Pattern 1 (the breakbeat), played by holding C3 for 4 bars on the
    // internal tempo, at three pad positions.
    outDir.createDirectory();
    struct Position { float x, y; const char* name; };
    for (const auto& p : { Position { 0.5f, 0.2f, "default" }, Position { 0.0f, 0.6f, "warm-hot" },
                           Position { 1.0f, 0.9f, "digital-destroyed" } })
    {
        auto params = defaultKitParams();
        params.global[gp::XyX] = p.x;
        params.global[gp::XyY] = p.y;

        constexpr int block = 256;
        const auto total = (int) (48000.0 * 8.5);
        Kit kit;
        kit.patternStore().edit ([] (PatternBank& b) { b.patterns[0] = breakbeatPattern(); });
        kit.setParameters (params);
        kit.prepare (48000.0, block);
        juce::AudioBuffer<float> out (2, total), buf (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            const auto n = std::min (block, total - pos);
            buf.setSize (2, n, false, false, true);
            juce::MidiBuffer midi;
            if (pos == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            if (pos <= 48000 * 8 && pos + n > 48000 * 8)
                midi.addEvent (juce::MidiMessage::noteOff (1, 60), 48000 * 8 - pos);
            kit.setParameters (params);
            kit.process (buf, midi);
            out.copyFrom (0, pos, buf, 0, 0, n);
            out.copyFrom (1, pos, buf, 1, 0, n);
        }
        const auto [rms, peak] = levelOfTail (out, 8.0);
        std::printf ("%-18s rms %6.1f dB  peak %6.1f dB\n", p.name, rms, peak);
        writeWav (outDir.getChildFile (juce::String ("breakbeat-") + p.name + ".wav"), out, 48000.0);
    }
    std::printf ("WAVs written to %s\n", outDir.getFullPathName().toRawUTF8());
    return 0;
}

int runBenchmark()
{
    // Worst case for phase 1: all 8 voices held at once, every operator active,
    // folding drive and a resonant filter. 48 kHz, 128-sample blocks. Then the
    // same with Stack: two sounds at 4 copies (a reese and a stab), and all 8.
    constexpr double sampleRate = 48000.0;
    constexpr int block = 128;
    constexpr double seconds = 20.0;

    auto run = [&] (std::array<int, kNumVoices> copies)
    {
        auto params = defaultKitParams();
        for (size_t i = 0; i < params.voices.size(); ++i)
        {
            auto& v = params.voices[i];
            v[vp::PlayMode] = 1.0f; // gate: sustain for the whole run
            v[vp::AmpS] = 1.0f;
            v[vp::FmAlgo] = 0.0f;
            v[vp::FmFeedback] = 0.5f;
            for (int o = 0; o < kNumOps; ++o)
            {
                v[opParam (o, OpLevel)] = 0.6f;
                v[opParam (o, OpS)] = 1.0f;
                v[opParam (o, Wave)] = 0.4f;
                v[opParam (o, Fold)] = 0.2f;
            }
            v[vp::Punch] = 0.5f;
            v[vp::Drive] = 0.5f;
            v[vp::DriveType] = 2.0f;
            v[vp::FltCutoff] = 3000.0f;
            v[vp::FltRes] = 0.5f;
            v[vp::Glide] = 50.0f;
            v[vp::StackCount] = (float) (copies[i] - 1);
        }

        Kit kit;
        kit.prepare (sampleRate, block);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;

        const auto blocks = (int) (seconds * sampleRate / block);
        kit.setParameters (params);
        for (int v = 0; v < kNumVoices; ++v)
            kit.noteOn (v, 48 + v, 1.0f);

        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < blocks; ++b)
        {
            params.voices[0][vp::FltCutoff] = 500.0f + 3000.0f * (float) ((b % 200) / 200.0); // moving filter
            kit.setParameters (params);
            kit.process (buf, midi);

            // A new note every quarter second on each voice, to include glides.
            if (b % 94 == 0)
                for (int v = 0; v < kNumVoices; ++v)
                    kit.noteOn (v, 48 + v + (b / 94) % 12, 1.0f);
        }
        const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - t0;
        return 100.0 * elapsedMs / (seconds * 1000.0);
    };

    const auto percent = run ({ 1, 1, 1, 1, 1, 1, 1, 1 });
    std::printf ("CPU: 8 voices, 4 ops each, fold drive, resonant filter @ 48 kHz / 128\n");
    std::printf ("  %.1f s of audio = %.2f%% of one core (real time)\n", seconds, percent);
    std::printf ("  per voice: %.2f%%\n", percent / kNumVoices);
    std::printf ("  with Stack 4 on two sounds: %.2f%%\n", run ({ 1, 1, 1, 1, 1, 4, 4, 1 }));
    std::printf ("  with Stack 4 on all eight:  %.2f%%\n", run ({ 4, 4, 4, 4, 4, 4, 4, 4 }));
    return 0;
}

} // namespace batida::tools
