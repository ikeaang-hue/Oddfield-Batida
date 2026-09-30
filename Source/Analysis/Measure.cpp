#include "Measure.h"

#include "Engine/SampleData.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace batida
{

namespace
{
// The BS.1770 K-weighting filter (a high shelf, then a high-pass) as two
// biquads. At 48 kHz these are the standard's own coefficients; at other rates
// they're derived from the same analogue design.
struct KWeighting
{
    double b0, b1, b2, a1, a2; // the shelf
    double c1, c2;             // the high-pass (numerator 1, -2, 1)
    double z1 = 0, z2 = 0, w1 = 0, w2 = 0;

    explicit KWeighting (double rate)
    {
        if (rate == 48000.0)
        {
            b0 = 1.53512485958697; b1 = -2.69169618940638; b2 = 1.19839281085285;
            a1 = -1.69065929318241; a2 = 0.73248077421585;
            c1 = -1.99004745483398; c2 = 0.99007225036621;
            return;
        }
        {
            const auto f0 = 1681.974450955533, gain = 3.999843853973347, q = 0.7071752369554196;
            const auto k = std::tan (juce::MathConstants<double>::pi * f0 / rate);
            const auto vh = std::pow (10.0, gain / 20.0), vb = std::pow (vh, 0.4996667741545416);
            const auto a0 = 1.0 + k / q + k * k;
            b0 = (vh + vb * k / q + k * k) / a0;
            b1 = 2.0 * (k * k - vh) / a0;
            b2 = (vh - vb * k / q + k * k) / a0;
            a1 = 2.0 * (k * k - 1.0) / a0;
            a2 = (1.0 - k / q + k * k) / a0;
        }
        {
            const auto f0 = 38.13547087602444, q = 0.5003270373238773;
            const auto k = std::tan (juce::MathConstants<double>::pi * f0 / rate);
            const auto a0 = 1.0 + k / q + k * k;
            c1 = 2.0 * (k * k - 1.0) / a0;
            c2 = (1.0 - k / q + k * k) / a0;
        }
    }

    double process (double in)
    {
        const auto s1 = b0 * in + z1;
        z1 = b1 * in - a1 * s1 + z2;
        z2 = b2 * in - a2 * s1;
        const auto s2 = s1 + w1;
        w1 = -2.0 * s1 - c1 * s2 + w2;
        w2 = s1 - c2 * s2;
        return s2;
    }
};

// An energy-weighted average power spectrum of a mono signal, up to `end`.
std::vector<double> averageSpectrum (const float* x, int n, int end, int order)
{
    const int size = 1 << order, hop = size / 2;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> hann ((size_t) size, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<double> power ((size_t) size / 2, 0.0);
    std::vector<float> frame ((size_t) size * 2);
    for (int start = 0; start < std::max (1, end - hop); start += hop)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);
        for (int i = 0; i < size && start + i < n; ++i)
            frame[(size_t) i] = x[start + i];
        hann.multiplyWithWindowingTable (frame.data(), (size_t) size);
        fft.performFrequencyOnlyForwardTransform (frame.data(), true);
        for (int k = 0; k < size / 2; ++k)
            power[(size_t) k] += (double) frame[(size_t) k] * frame[(size_t) k];
    }
    return power;
}

float toDb (double power) { return (float) (10.0 * std::log10 (std::max (1.0e-12, power))); }
} // namespace

HitMeasures measureHit (const float* x, int n, double rate)
{
    HitMeasures f;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        if (! std::isfinite (x[i]))
            f.finite = false;
        else
            peak = std::max (peak, std::abs (x[i]));
    }
    if (! f.finite || peak <= 0.0f)
        return f;
    f.peakDb = 20.0f * std::log10 (peak);

    // Loudness: K-weighted, the loudest 50 ms, so a clap and a snare of the
    // same loudness sound about as loud.
    std::vector<double> k ((size_t) n);
    {
        KWeighting filter (rate);
        for (int i = 0; i < n; ++i)
            k[(size_t) i] = filter.process (x[i]);
    }
    const auto window = std::max (1, (int) (0.05 * rate));
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
        if (std::abs (x[i]) > peak * 0.001f)
            last = i;
    f.lengthMs = (float) (last * 1000.0 / rate);
    f.crestDb = f.peakDb - f.loudnessDb;

    // Pitch: zero crossings from 60 ms (past most pitch sweeps) over up to 200 ms.
    {
        const auto from = std::min (last, (int) (0.06 * rate));
        const auto to = std::min (last, from + (int) (0.2 * rate));
        int crossings = 0;
        for (int i = from + 1; i < to; ++i)
            if ((x[i - 1] < 0.0f) != (x[i] < 0.0f))
                ++crossings;
        if (to - from > (int) (0.02 * rate))
            f.pitchHz = (float) (crossings * 0.5 * rate / (to - from));
    }

    constexpr int order = 12, size = 1 << order;
    const auto power = averageSpectrum (x, n, std::min (n, last + (int) (0.05 * rate)), order);

    const auto binHz = rate / size;
    double total = 0.0, weighted = 0.0, low = 0.0, high = 0.0, logSum = 0.0, linSum = 0.0;
    int flatBins = 0;
    for (int b = 1; b < size / 2; ++b)
    {
        const auto hz = b * binHz;
        const auto pw = power[(size_t) b];
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
    for (int b = 2; b < size / 2 - 1; ++b)
        if (b * binHz < 9000.0)
            maxPower = std::max (maxPower, power[(size_t) b]);
    for (int b = 2; b < size / 2 - 1; ++b)
    {
        const auto hz = b * binHz;
        if (hz < 30.0 || hz > 9000.0)
            continue;
        const auto pw = power[(size_t) b];
        if (pw > power[(size_t) b - 1] && pw >= power[(size_t) b + 1] && pw > maxPower * 0.01)
        {
            // Parabolic interpolation on dB for a finer frequency.
            const auto a = std::log (power[(size_t) b - 1] + 1e-20), c0 = std::log (pw + 1e-20), c = std::log (power[(size_t) b + 1] + 1e-20);
            const auto d = 0.5 * (a - c) / (a - 2 * c0 + c);
            peaks.push_back ({ (b + (std::isfinite (d) ? d : 0.0)) * binHz, pw });
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

AudioMeasures measureAudio (const juce::AudioBuffer<float>& audio, double rate)
{
    AudioMeasures m;
    m.sampleRate = rate;
    m.channels = audio.getNumChannels();
    const auto n = audio.getNumSamples();
    m.seconds = n / rate;
    if (n == 0 || m.channels == 0)
        return m;

    float peak = 0.0f;
    for (int ch = 0; ch < m.channels; ++ch)
        for (int i = 0; i < n; ++i)
        {
            const auto v = audio.getSample (ch, i);
            if (! std::isfinite (v))
            {
                m.finite = false;
                continue;
            }
            peak = std::max (peak, std::abs (v));
            if (std::abs (v) >= 0.99999f)
                ++m.clipped;
        }
    if (! m.finite)
        return m;
    m.peakDb = peak > 0.0f ? 20.0f * std::log10 (peak) : -120.0f;

    // The mono sum: what a hit's length, tone and pitch are measured on.
    std::vector<float> mono ((size_t) n, 0.0f);
    for (int ch = 0; ch < m.channels; ++ch)
        juce::FloatVectorOperations::addWithMultiply (mono.data(), audio.getReadPointer (ch), 1.0f / (float) m.channels, n);
    m.sound = measureHit (mono.data(), n, rate);
    if (peak <= 0.0f)
        return m;

    // True peak: 4x oversampled.
    {
        juce::dsp::Oversampling<float> os ((size_t) m.channels, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
        constexpr int block = 1024;
        os.initProcessing (block);
        juce::AudioBuffer<float> chunk (m.channels, block);
        float truePeak = peak;
        for (int pos = 0; pos < n + block; pos += block) // one block of silence flushes the filters
        {
            chunk.clear();
            const auto count = std::max (0, std::min (block, n - pos));
            for (int ch = 0; ch < m.channels && count > 0; ++ch)
                chunk.copyFrom (ch, 0, audio, ch, pos, count);
            juce::dsp::AudioBlock<float> in (chunk);
            const auto up = os.processSamplesUp (in);
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                    truePeak = std::max (truePeak, std::abs (up.getSample ((int) ch, (int) i)));
        }
        m.truePeakDb = 20.0f * std::log10 (truePeak);
    }

    // Loudness: BS.1770, 400 ms blocks every 100 ms, gated at -70 LUFS and
    // then 10 LU under the ungated mean.
    {
        std::vector<double> power ((size_t) n, 0.0);
        for (int ch = 0; ch < m.channels; ++ch)
        {
            KWeighting filter (rate);
            const auto* x = audio.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const auto y = filter.process (x[i]);
                power[(size_t) i] += y * y;
            }
        }
        const auto window = std::min (n, (int) (0.4 * rate)), hop = std::max (1, (int) (0.1 * rate));
        std::vector<double> blocks;
        double sum = 0.0;
        for (int i = 0; i < window; ++i)
            sum += power[(size_t) i];
        for (int start = 0;; start += hop)
        {
            blocks.push_back (sum / window);
            if (start + hop + window > n)
                break;
            for (int i = start; i < start + hop; ++i)
                sum += power[(size_t) (i + window)] - power[(size_t) i];
        }
        auto lufs = [] (double p) { return -0.691 + 10.0 * std::log10 (std::max (1.0e-12, p)); };
        double loudest = 0.0;
        for (const auto b : blocks)
            loudest = std::max (loudest, b);
        m.loudestLufs = (float) lufs (loudest);

        auto gatedMean = [&] (double threshold)
        {
            double total = 0.0;
            int count = 0;
            for (const auto b : blocks)
                if (lufs (b) > threshold)
                {
                    total += b;
                    ++count;
                }
            return count > 0 ? total / count : 0.0;
        };
        const auto ungated = gatedMean (-70.0);
        if (ungated > 0.0)
            m.loudnessLufs = (float) lufs (gatedMean (lufs (ungated) - 10.0));
    }

    // Width: side energy over mid and side.
    if (m.channels == 2)
    {
        double mid = 0.0, side = 0.0;
        const auto* l = audio.getReadPointer (0);
        const auto* r = audio.getReadPointer (1);
        for (int i = 0; i < n; ++i)
        {
            const double a = 0.5 * (l[i] + r[i]), b = 0.5 * (l[i] - r[i]);
            mid += a * a;
            side += b * b;
        }
        if (mid + side > 0.0)
            m.widthPercent = (float) (100.0 * side / (mid + side));
    }

    // Octave bands of the whole file.
    {
        constexpr int order = 12, size = 1 << order;
        const auto power = averageSpectrum (mono.data(), n, n, order);
        const auto binHz = rate / size;
        double total = 0.0;
        std::array<double, AudioMeasures::kBands> bands {};
        for (int b = 1; b < size / 2; ++b)
        {
            const auto hz = b * binHz;
            total += power[(size_t) b];
            for (int i = 0; i < AudioMeasures::kBands; ++i)
                if (hz >= AudioMeasures::kBandHz[(size_t) i] / std::sqrt (2.0) && hz < AudioMeasures::kBandHz[(size_t) i] * std::sqrt (2.0))
                    bands[(size_t) i] += power[(size_t) b];
        }
        for (int i = 0; i < AudioMeasures::kBands; ++i)
            m.bandsDb[(size_t) i] = total > 0.0 ? toDb (bands[(size_t) i] / total) : -120.0f;
    }

    // Onsets, found as transient slicing finds them.
    {
        SampleData data;
        data.audio.makeCopyOf (audio);
        data.sampleRate = rate;
        computeOnsets (data);
        for (const auto& [frame, strength] : data.onsets)
            m.onsets.push_back ({ frame / rate, strength });
    }
    return m;
}

int noteNumberFor (float hz)
{
    return hz > 0.0f ? (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0)) : -1;
}

juce::String noteName (int note)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (note < 0 || note > 127)
        return {};
    return juce::String (names[note % 12]) + juce::String (note / 12 - 2);
}

} // namespace batida
