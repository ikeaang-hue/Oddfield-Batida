#include "SampleData.h"

namespace batida
{

namespace
{
void computePeaks (SampleData& data)
{
    const auto frames = data.numFrames();
    data.peaks.assign ((size_t) SampleSlot::kPeakBins, { 0.0f, 0.0f });
    if (frames == 0)
        return;

    for (int bin = 0; bin < SampleSlot::kPeakBins; ++bin)
    {
        const auto start = (int) ((int64_t) bin * frames / SampleSlot::kPeakBins);
        const auto end = std::max (start + 1, (int) ((int64_t) (bin + 1) * frames / SampleSlot::kPeakBins));
        float lo = 0.0f, hi = 0.0f;

        for (int ch = 0; ch < data.numChannels(); ++ch)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (
                data.audio.getReadPointer (ch, start), std::min (end, frames) - start);
            lo = std::min (lo, range.getStart());
            hi = std::max (hi, range.getEnd());
        }
        data.peaks[(size_t) bin] = { lo, hi };
    }
}
// Onsets for transient slicing: rises in high-passed energy, found once when
// the sample loads. Strength is relative to the strongest onset in the file,
// so the sensitivity knob works the same on quiet and loud material.
void computeOnsets (SampleData& data)
{
    data.onsets.clear();
    const auto frames = data.numFrames();
    constexpr int hop = 256, window = 512;
    if (frames < window * 2)
        return;

    std::vector<float> level;
    for (int start = 0; start + window <= frames; start += hop)
    {
        double e = 0.0;
        for (int ch = 0; ch < data.numChannels(); ++ch)
        {
            const auto* x = data.audio.getReadPointer (ch);
            for (int i = std::max (1, start); i < start + window; ++i)
            {
                const auto d = x[i] - 0.9f * x[i - 1]; // tilt towards transients
                e += (double) d * d;
            }
        }
        level.push_back ((float) (10.0 * std::log10 (e / window + 1.0e-10)));
    }

    std::vector<float> rise (level.size(), 0.0f);
    for (size_t k = 1; k < level.size(); ++k)
        rise[k] = std::max (0.0f, level[k] - level[k - 1]);

    const auto minGap = (int) (0.05 * data.sampleRate / hop); // 50 ms
    float strongest = 0.0f;
    std::vector<std::pair<int, float>> found;
    for (size_t k = 1; k < rise.size(); ++k)
    {
        if (rise[k] < 3.0f)
            continue;
        bool peak = true;
        for (int j = -3; j <= 3 && peak; ++j)
        {
            const auto n = (int) k + j;
            if (j != 0 && n >= 0 && n < (int) rise.size() && rise[(size_t) n] > rise[k])
                peak = false;
        }
        if (! peak || (! found.empty() && (int) k * hop - found.back().first < minGap * hop))
            continue;
        found.push_back ({ (int) k * hop, rise[k] });
        strongest = std::max (strongest, rise[k]);
    }

    // Refine each onset from its analysis window to the attack itself: the first
    // sample reaching a quarter of the window's peak, less a short pre-roll.
    for (auto& [frame, strength] : found)
    {
        const auto end = std::min (frames, frame + window + hop);
        float peak = 0.0f;
        for (int ch = 0; ch < data.numChannels(); ++ch)
            for (int i = frame; i < end; ++i)
                peak = std::max (peak, std::abs (data.audio.getSample (ch, i)));

        auto attack = frame;
        for (int i = frame; i < end; ++i)
        {
            float v = 0.0f;
            for (int ch = 0; ch < data.numChannels(); ++ch)
                v = std::max (v, std::abs (data.audio.getSample (ch, i)));
            if (v >= 0.25f * peak)
            {
                attack = i;
                break;
            }
        }
        data.onsets.push_back ({ std::max (0, attack - 32), strongest > 0.0f ? strength / strongest : 0.0f });
    }
}
} // namespace

std::unique_ptr<SampleData> SampleSlot::decode (const juce::File& file, juce::AudioFormatManager& formats, juce::String& note)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return nullptr;

    const auto channels = (int) std::min (2u, reader->numChannels);
    const auto maxFrames = (juce::int64) (kMaxSeconds * reader->sampleRate);
    const auto frames = (int) std::min (reader->lengthInSamples, maxFrames);
    if (frames <= 0)
        return nullptr;

    auto data = std::make_unique<SampleData>();
    data->audio.setSize (channels, frames);
    reader->read (&data->audio, 0, frames, 0, true, channels > 1);
    data->sampleRate = reader->sampleRate;
    data->path = file.getFullPathName();
    computePeaks (*data);
    computeOnsets (*data);
    note = reader->lengthInSamples > maxFrames ? "Trimmed to 2 minutes" : "";
    return data;
}

void SampleSlot::setDecoded (std::unique_ptr<SampleData> data, const juce::String& note)
{
    if (data == nullptr)
        return;
    {
        const juce::ScopedLock sl (lock);
        status = Status::Loaded;
        path = data->path;
        error = note;
    }
    publish (std::move (data));
}

bool SampleSlot::load (const juce::File& file, juce::AudioFormatManager& formats)
{
    juce::String note;
    auto data = decode (file, formats, note);

    if (data == nullptr)
    {
        if (! file.existsAsFile())
            markMissing (file.getFullPathName());
        else
        {
            const juce::ScopedLock sl (lock);
            error = "Can't read " + file.getFileName();
        }
        return false;
    }

    setDecoded (std::move (data), note);
    return true;
}

bool SampleSlot::setData (std::unique_ptr<SampleData> data)
{
    if (data == nullptr || data->numFrames() == 0)
        return false;

    computePeaks (*data);
    computeOnsets (*data);
    {
        const juce::ScopedLock sl (lock);
        status = Status::Loaded;
        path = data->path;
        error = {};
    }
    publish (std::move (data));
    return true;
}

void SampleSlot::markMissing (const juce::String& missingPath)
{
    {
        const juce::ScopedLock sl (lock);
        status = Status::Missing;
        path = missingPath;
        error = {};
    }
    publish (nullptr);
}

void SampleSlot::markUnreadable (const juce::String& unreadablePath)
{
    {
        const juce::ScopedLock sl (lock);
        status = Status::Error;
        path = unreadablePath;
        error = "Can't read " + juce::File (unreadablePath).getFileName();
    }
    publish (nullptr);
}

std::shared_ptr<const SampleData> SampleSlot::share() const
{
    const juce::ScopedLock sl (lock);
    const auto* current = latest.load();
    for (const auto& entry : pool)
        if (entry.data.get() == current)
            return entry.data;
    return nullptr;
}

void SampleSlot::clear()
{
    {
        const juce::ScopedLock sl (lock);
        status = Status::Empty;
        path = {};
        error = {};
    }
    publish (nullptr);
}

void SampleSlot::setShared (std::shared_ptr<const SampleData> data)
{
    if (data == nullptr)
        return;
    {
        const juce::ScopedLock sl (lock);
        status = Status::Loaded;
        path = data->path;
        error = {};
    }
    publish (std::move (data));
}

void SampleSlot::publish (std::shared_ptr<const SampleData> data)
{
    const juce::ScopedLock sl (lock);
    const auto now = juce::Time::getMillisecondCounterHiRes();

    for (auto& entry : pool)
        if (entry.retiredAt == 0.0)
            entry.retiredAt = now;

    auto* raw = data.get();
    if (data != nullptr)
        pool.push_back ({ std::move (data), 0.0 });

    latest.store (raw, std::memory_order_release);
    ++version;
    collectGarbage();
}

void SampleSlot::collectGarbage()
{
    const juce::ScopedLock sl (lock);
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto* current = latest.load();
    const auto* used = inUse.load();

    pool.erase (std::remove_if (pool.begin(), pool.end(), [&] (const Entry& e)
                                {
                                    return e.retiredAt > 0.0 && now - e.retiredAt > 1000.0
                                        && e.data.get() != current && e.data.get() != used;
                                }),
                pool.end());
}

} // namespace batida
