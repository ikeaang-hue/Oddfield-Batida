#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>
#include <vector>

namespace batida
{

struct SampleData
{
    juce::AudioBuffer<float> audio; // 1 or 2 channels
    double sampleRate = 44100.0;
    juce::String path;
    std::vector<std::pair<float, float>> peaks; // min/max per display bin
    std::vector<std::pair<int, float>> onsets;  // frame, strength 0..1 (for Transients slicing)

    int numFrames() const { return audio.getNumSamples(); }
    int numChannels() const { return audio.getNumChannels(); }
};

// Holds one voice's sample and hands it to the audio thread without locks.
//
// The message thread loads files and keeps ownership of every buffer. The
// audio thread picks up the latest pointer once per block and reports what it
// is using; buffers are freed on the message thread only after they have been
// out of use for a while, so the audio thread never allocates or frees.
class SampleSlot
{
public:
    enum class Status { Empty, Loaded, Missing, Error };

    static constexpr double kMaxSeconds = 120.0;
    static constexpr int kPeakBins = 512;

    // Message thread ------------------------------------------------------
    bool load (const juce::File& file, juce::AudioFormatManager& formats);
    bool setData (std::unique_ptr<SampleData> data); // for tests and generated audio
    void markMissing (const juce::String& path);     // keeps the path so it is saved again
    void clear();
    void collectGarbage();

    Status getStatus() const { const juce::ScopedLock sl (lock); return status; }
    juce::String getPath() const { const juce::ScopedLock sl (lock); return path; }
    juce::String getError() const { const juce::ScopedLock sl (lock); return error; }
    const SampleData* getDisplayData() const { return latest.load(); }
    int getVersion() const { return version.load(); }

    // Audio thread --------------------------------------------------------
    const SampleData* acquire()
    {
        auto* data = latest.load (std::memory_order_acquire);
        inUse.store (data, std::memory_order_release);
        return data;
    }

private:
    void publish (std::unique_ptr<SampleData> data);

    struct Entry
    {
        std::unique_ptr<SampleData> data;
        double retiredAt = 0.0; // 0 = still current
    };

    juce::CriticalSection lock; // message-side state only, never taken on the audio thread
    std::vector<Entry> pool;
    std::atomic<SampleData*> latest { nullptr }, inUse { nullptr };
    std::atomic<int> version { 0 };

    Status status = Status::Empty;
    juce::String path, error;
};

} // namespace batida
