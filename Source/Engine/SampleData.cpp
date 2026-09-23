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
} // namespace

bool SampleSlot::load (const juce::File& file, juce::AudioFormatManager& formats)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
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

    const auto channels = (int) std::min (2u, reader->numChannels);
    const auto maxFrames = (juce::int64) (kMaxSeconds * reader->sampleRate);
    const auto frames = (int) std::min (reader->lengthInSamples, maxFrames);

    auto data = std::make_unique<SampleData>();
    data->audio.setSize (channels, frames);
    reader->read (&data->audio, 0, frames, 0, true, channels > 1);
    data->sampleRate = reader->sampleRate;
    data->path = file.getFullPathName();
    computePeaks (*data);

    {
        const juce::ScopedLock sl (lock);
        status = Status::Loaded;
        path = file.getFullPathName();
        error = reader->lengthInSamples > maxFrames ? "Trimmed to 2 minutes" : "";
    }
    publish (std::move (data));
    return true;
}

bool SampleSlot::setData (std::unique_ptr<SampleData> data)
{
    if (data == nullptr || data->numFrames() == 0)
        return false;

    computePeaks (*data);
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

void SampleSlot::publish (std::unique_ptr<SampleData> data)
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
