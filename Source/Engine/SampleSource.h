#pragma once

#include "SampleData.h"

namespace batida
{

// Plays one voice's sample. Pitch changes speed (no time-stretching).
class SampleSource
{
public:
    struct Settings
    {
        float start = 0.0f, end = 1.0f;          // 0..1 of the file
        float loopStart = 0.0f, loopEnd = 1.0f;  // 0..1 of the file
        float fadeInMs = 0.0f, fadeOutMs = 0.0f;
        float gain = 1.0f;
        bool reverse = false, loop = false, gate = false;
    };

    void prepare (double hostSampleRate);
    void setData (const SampleData* newData);     // once per block
    void setSettings (const Settings& newSettings);
    void trigger();
    void reset();

    bool isActive() const { return playing; }

    // One stereo frame; pitchRatio 1 = original speed.
    void process (float pitchRatio, float& left, float& right);

private:
    float read (int channel, double position) const;

    const SampleData* data = nullptr;
    Settings settings;
    double hostRate = 48000.0;
    double pos = 0.0;
    double elapsed = 0.0;
    bool playing = false;
};

} // namespace batida
