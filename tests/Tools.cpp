#include "Tools.h"

#include <cmath>

namespace batida::tools
{

juce::AudioBuffer<float> renderVoice (const KitParams& params, int voice, int key, float velocity,
                                      double seconds, double sampleRate, double noteLengthSeconds)
{
    constexpr int block = 128;
    Kit kit;
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

int runBenchmark()
{
    // Worst case for phase 1: all 8 voices held at once, every operator active,
    // folding drive and a resonant filter. 48 kHz, 128-sample blocks.
    constexpr double sampleRate = 48000.0;
    constexpr int block = 128;
    constexpr double seconds = 20.0;

    auto params = defaultKitParams();
    for (auto& v : params.voices)
    {
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
    const auto percent = 100.0 * elapsedMs / (seconds * 1000.0);

    std::printf ("CPU: 8 voices, 4 ops each, fold drive, resonant filter @ 48 kHz / 128\n");
    std::printf ("  %.1f s of audio in %.0f ms = %.2f%% of one core (real time)\n", seconds, elapsedMs, percent);
    std::printf ("  per voice: %.2f%%\n", percent / kNumVoices);
    return 0;
}

} // namespace batida::tools
