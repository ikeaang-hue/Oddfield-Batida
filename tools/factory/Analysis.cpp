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
    return measureHit (x.data(), (int) x.size(), kRate);
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
        xml->writeTo (presetFile, presetTextFormat());
}

} // namespace batida::factory
