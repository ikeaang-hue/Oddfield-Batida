#include "Factory.h"

#include "Engine/ParamRange.h"

#include <juce_dsp/juce_dsp.h>

#include <cassert>
#include <cmath>

namespace batida::factory
{

void apply (VoiceParams& p, const Overrides& o)
{
    for (const auto& [key, value] : o)
    {
        const auto i = findVoiceParam (key);
        jassert (i >= 0);
        if (i < 0)
        {
            std::fprintf (stderr, "unknown voice parameter %s\n", key);
            std::abort();
        }
        const auto& spec = voiceParamSpecs()[(size_t) i];
        p[i] = std::clamp (value, spec.min, spec.max);
    }
}

VoiceParams voiceFrom (const Overrides& o)
{
    VoiceParams p;
    for (int i = 0; i < kNumVoiceParams; ++i)
        p[i] = voiceParamSpecs()[(size_t) i].def;
    apply (p, o);
    return p;
}

void applyGlobals (std::array<float, kNumGlobalParams>& g, const Overrides& o)
{
    for (const auto& [key, value] : o)
    {
        int found = -1;
        for (int i = 0; i < kNumGlobalParams; ++i)
            if (globalParamID (i) == key)
                found = i;
        if (found < 0)
        {
            std::fprintf (stderr, "unknown kit parameter %s\n", key);
            std::abort();
        }
        const auto& spec = globalParamSpecs()[(size_t) found];
        g[(size_t) found] = std::clamp (value, spec.min, spec.max);
    }
}

std::vector<float> renderHit (const VoiceParams& p, const SampleData* sample, double seconds, double gateSeconds, int key)
{
    constexpr int block = 256;
    const auto total = (int) (seconds * kRate);
    const auto offAt = (int) (gateSeconds * kRate);

    Voice voice;
    voice.prepare (kRate);
    voice.setSampleData (sample);
    voice.setParameters (p);
    voice.noteOn (key, 0.85f);

    std::vector<float> l ((size_t) total, 0.0f), r ((size_t) total, 0.0f);
    for (int pos = 0; pos < total; pos += block)
    {
        const auto n = std::min (block, total - pos);
        if (offAt >= pos && offAt < pos + n)
            voice.noteOff (key);
        voice.render (l.data() + pos, r.data() + pos, l.data() + pos, r.data() + pos, n);
        if (! voice.isActive() && pos > offAt)
            break;
    }
    for (size_t i = 0; i < l.size(); ++i)
        l[i] = 0.5f * (l[i] + r[i]);
    return l;
}

Features analyse (const std::vector<float>& x)
{
    Features f;
    const auto n = (int) x.size();
    float peak = 0.0f;
    for (auto v : x)
    {
        if (! std::isfinite (v))
            f.finite = false;
        else
            peak = std::max (peak, std::abs (v));
    }
    if (! f.finite || peak <= 0.0f)
        return f;
    f.peakDb = 20.0f * std::log10 (peak);

    // Loudness: K-weighted (ITU BS.1770 filters at 48 kHz), the loudest 50 ms,
    // so a clap and a snare of the same loudness sound about as loud.
    std::vector<double> k ((size_t) n);
    {
        double z1 = 0, z2 = 0, w1 = 0, w2 = 0;
        for (int i = 0; i < n; ++i)
        {
            const double in = x[(size_t) i];
            const auto s1 = 1.53512485958697 * in + z1;
            z1 = -2.69169618940638 * in - -1.69065929318241 * s1 + z2;
            z2 = 1.19839281085285 * in - 0.73248077421585 * s1;
            const auto s2 = s1 + w1;
            w1 = -2.0 * s1 - -1.99004745483398 * s2 + w2;
            w2 = s1 - 0.99007225036621 * s2;
            k[(size_t) i] = s2;
        }
    }
    const auto window = (int) (0.05 * kRate);
    double sum = 0.0, loudest = 0.0;
    for (int i = 0; i < n; ++i)
    {
        sum += k[(size_t) i] * k[(size_t) i];
        if (i >= window)
            sum -= k[(size_t) (i - window)] * k[(size_t) (i - window)];
        loudest = std::max (loudest, sum);
    }
    f.loudnessDb = (float) (10.0 * std::log10 (std::max (1.0e-12, loudest / window)));

    int last = 0;
    for (int i = 0; i < n; ++i)
        if (std::abs (x[(size_t) i]) > peak * 0.001f)
            last = i;
    f.lengthMs = (float) (last * 1000.0 / kRate);
    f.crestDb = f.peakDb - f.loudnessDb;

    // Pitch: zero crossings from 60 ms (past most pitch sweeps) over up to 200 ms.
    {
        const auto from = std::min (last, (int) (0.06 * kRate));
        const auto to = std::min (last, from + (int) (0.2 * kRate));
        int crossings = 0;
        for (int i = from + 1; i < to; ++i)
            if ((x[(size_t) i - 1] < 0.0f) != (x[(size_t) i] < 0.0f))
                ++crossings;
        if (to - from > (int) (0.02 * kRate))
            f.pitchHz = (float) (crossings * 0.5 * kRate / (to - from));
    }

    // An energy-weighted average spectrum over the sound.
    constexpr int order = 12, size = 1 << order, hop = size / 2;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> hann ((size_t) size, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<double> power ((size_t) size / 2, 0.0);
    std::vector<float> frame ((size_t) size * 2);
    const auto end = std::min (n, last + (int) (0.05 * kRate));
    for (int start = 0; start < std::max (1, end - hop); start += hop)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);
        for (int i = 0; i < size && start + i < n; ++i)
            frame[(size_t) i] = x[(size_t) (start + i)];
        hann.multiplyWithWindowingTable (frame.data(), (size_t) size);
        fft.performFrequencyOnlyForwardTransform (frame.data(), true);
        for (int k = 0; k < size / 2; ++k)
            power[(size_t) k] += (double) frame[(size_t) k] * frame[(size_t) k];
    }

    const auto binHz = kRate / size;
    double total = 0.0, weighted = 0.0, low = 0.0, high = 0.0, logSum = 0.0, linSum = 0.0;
    int flatBins = 0;
    for (int k = 1; k < size / 2; ++k)
    {
        const auto hz = k * binHz;
        const auto pw = power[(size_t) k];
        total += pw;
        weighted += pw * hz;
        if (hz < 150.0) low += pw;
        if (hz > 6000.0) high += pw;
        if (hz >= 200.0 && hz <= 16000.0)
        {
            logSum += std::log (pw + 1.0e-20);
            linSum += pw;
            ++flatBins;
        }
    }
    if (total <= 0.0)
        return f;
    f.centroidHz = (float) (weighted / total);
    f.low = (float) (low / total);
    f.high = (float) (high / total);
    if (flatBins > 0 && linSum > 0.0)
        f.flatness = (float) std::clamp (std::exp (logSum / flatBins) / (linSum / flatBins), 0.0, 1.0);

    // Inharmonicity: the strongest peaks, against the best fundamental among
    // the strongest peak and its 1/2 and 1/3.
    struct Peak { double hz, power; };
    std::vector<Peak> peaks;
    double maxPower = 0.0;
    for (int k = 2; k < size / 2 - 1; ++k)
        if (k * binHz < 9000.0)
            maxPower = std::max (maxPower, power[(size_t) k]);
    for (int k = 2; k < size / 2 - 1; ++k)
    {
        const auto hz = k * binHz;
        if (hz < 30.0 || hz > 9000.0)
            continue;
        const auto pw = power[(size_t) k];
        if (pw > power[(size_t) k - 1] && pw >= power[(size_t) k + 1] && pw > maxPower * 0.01)
        {
            // Parabolic interpolation on dB for a finer frequency.
            const auto a = std::log (power[(size_t) k - 1] + 1e-20), b = std::log (pw + 1e-20), c = std::log (power[(size_t) k + 1] + 1e-20);
            const auto d = 0.5 * (a - c) / (a - 2 * b + c);
            peaks.push_back ({ (k + (std::isfinite (d) ? d : 0.0)) * binHz, pw });
        }
    }
    std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.power > b.power; });
    if (peaks.size() > 8)
        peaks.resize (8);
    if (peaks.size() >= 2)
    {
        double best = 1.0;
        for (const auto div : { 1.0, 2.0, 3.0 })
        {
            const auto f0 = peaks.front().hz / div;
            if (f0 < 25.0)
                continue;
            double miss = 0.0, weight = 0.0;
            for (const auto& pk : peaks)
            {
                const auto r = pk.hz / f0;
                const auto h = std::max (1.0, std::round (r));
                miss += pk.power * std::min (0.5, std::abs (r - h) / std::max (1.0, h * 0.06) * 0.5);
                weight += pk.power;
            }
            best = std::min (best, miss / weight * 2.0 + (div > 1.0 ? 0.05 : 0.0));
        }
        f.inharmonic = (float) std::clamp (best, 0.0, 1.0);
    }
    return f;
}

Features analyse (const VoiceParams& p, const SampleData* sample)
{
    return analyse (renderHit (p, sample));
}

float distance (const Features& a, const Features& b)
{
    const auto length = std::log2 ((a.lengthMs + 20.0f) / (b.lengthMs + 20.0f)) / 0.5f;
    const auto bright = std::log2 ((a.centroidHz + 50.0f) / (b.centroidHz + 50.0f)) / 0.35f;
    const auto low = (a.low - b.low) / 0.15f;
    const auto flat = (a.flatness - b.flatness) / 0.12f;
    const auto inh = (a.inharmonic - b.inharmonic) / 0.25f;
    const auto tonal = std::max (0.0f, 1.0f - 2.0f * std::max (a.flatness, b.flatness));
    const auto pitch = a.pitchHz > 0.0f && b.pitchHz > 0.0f ? tonal * 12.0f * std::log2 (a.pitchHz / b.pitchHz) / 2.0f : 0.0f;
    const auto crest = (a.crestDb - b.crestDb) / 4.0f;
    return std::sqrt (length * length + bright * bright + low * low + flat * flat + inh * inh + pitch * pitch + crest * crest);
}

// Rendering a kit ----------------------------------------------------------------

juce::AudioBuffer<float> renderKit (const KitPreset& preset, const std::array<std::shared_ptr<SampleSlot>, kNumVoices>& samples,
                                    const Pattern& pattern, float tempo, float swing, int bars, const Overrides& extra)
{
    constexpr int block = 128;
    KitParams params;
    params.voices = preset.voices;
    params.global = preset.globals;
    for (int g = 0; g < kNumGlobalParams; ++g)
        if (! isKitGlobal (g))
            params.global[(size_t) g] = globalParamSpecs()[(size_t) g].def;
    params.global[gp::SeqRun] = 1.0f;  // Transport
    params.global[gp::SeqPlay] = 1.0f; // the internal play, the host being stopped
    params.global[gp::SeqSync] = 0.0f;
    params.global[gp::SeqTempo] = tempo;
    params.global[gp::SeqSwing] = swing;
    params.global[gp::SeqPattern] = 0.0f;
    params.global[gp::SeqQuantise] = 0.0f;
    applyGlobals (params.global, extra);

    Kit kit;
    PatternBank bank;
    bank.patterns[0] = pattern;
    kit.patternStore().replace (bank);
    kit.movementStore().replace (preset.movement);
    for (int v = 0; v < kNumVoices; ++v)
        if (samples[(size_t) v] != nullptr)
            if (const auto* d = samples[(size_t) v]->getDisplayData())
            {
                auto copy = std::make_unique<SampleData> (*d);
                kit.sampleSlot (v).setData (std::move (copy));
            }
    kit.setParameters (params);
    kit.prepare (kRate, block);

    const auto barSeconds = 4.0 * 60.0 / tempo;
    const auto total = (int) ((bars * barSeconds + 0.5) * kRate);
    juce::AudioBuffer<float> out (2, total), buf (2, block);
    for (int pos = 0; pos < total; pos += block)
    {
        const auto n = std::min (block, total - pos);
        buf.setSize (2, n, false, false, true);
        kit.setParameters (params);
        kit.process (buf, {});
        out.copyFrom (0, pos, buf, 0, 0, n);
        out.copyFrom (1, pos, buf, 1, 0, n);
    }
    return out;
}

float beatLoudnessDb (const juce::AudioBuffer<float>& audio)
{
    // K-weighted (BS.1770) loudness in 400 ms windows, every 100 ms, after
    // the first quarter; the mean power of the loudest 30% of windows. So a
    // sparse beat and a busy one are matched by how loud their hits are, not
    // by how many there are.
    const auto n = audio.getNumSamples();
    std::vector<double> power ((size_t) n, 0.0);
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
    {
        double z1 = 0, z2 = 0, w1 = 0, w2 = 0;
        const auto* x = audio.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const double in = x[i];
            const auto s1 = 1.53512485958697 * in + z1;
            z1 = -2.69169618940638 * in - -1.69065929318241 * s1 + z2;
            z2 = 1.19839281085285 * in - 0.73248077421585 * s1;
            const auto s2 = s1 + w1;
            w1 = -2.0 * s1 - -1.99004745483398 * s2 + w2;
            w2 = s1 - 0.99007225036621 * s2;
            power[(size_t) i] += s2 * s2;
        }
    }
    const auto window = (int) (0.4 * kRate), hop = (int) (0.1 * kRate);
    std::vector<double> windows;
    for (int start = n / 4; start + window <= n; start += hop)
    {
        double sum = 0.0;
        for (int i = start; i < start + window; ++i)
            sum += power[(size_t) i];
        windows.push_back (sum / window);
    }
    if (windows.empty())
        return -120.0f;
    std::sort (windows.begin(), windows.end(), std::greater<>());
    const auto count = std::max<size_t> (1, windows.size() * 3 / 10);
    double mean = 0.0;
    for (size_t i = 0; i < count; ++i)
        mean += windows[i];
    return (float) (10.0 * std::log10 (std::max (1.0e-12, mean / (double) count)));
}

float peakDb (const juce::AudioBuffer<float>& audio)
{
    float peak = 0.0f;
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        peak = std::max (peak, audio.getMagnitude (ch, 0, audio.getNumSamples()));
    return peak > 0.0f ? 20.0f * std::log10 (peak) : -120.0f;
}

// Files ----------------------------------------------------------------------------

bool writeFlac (const juce::File& file, const juce::AudioBuffer<float>& audio)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;
    const auto options = juce::AudioFormatWriterOptions {}
                             .withSampleRate (kRate)
                             .withNumChannels (audio.getNumChannels())
                             .withBitsPerSample (24);
    auto writer = flac.createWriterFor (stream, options);
    if (writer == nullptr)
        return false;
    return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

void stripAbsolutePaths (const juce::File& presetFile)
{
    auto xml = juce::XmlDocument::parse (presetFile);
    if (xml == nullptr)
        return;
    bool changed = false;
    std::function<void (juce::XmlElement&)> visit = [&] (juce::XmlElement& e)
    {
        if (e.hasTagName ("SAMPLE") && e.hasAttribute ("absolute"))
        {
            e.removeAttribute ("absolute");
            changed = true;
        }
        for (auto* c : e.getChildIterator())
            visit (*c);
    };
    visit (*xml);
    if (changed)
        xml->writeTo (presetFile);
}

} // namespace batida::factory
