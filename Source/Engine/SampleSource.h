#pragma once

#include "Parameters.h"
#include "SampleData.h"

namespace batida
{

// Slices of the region [start, end) (frames): Grid cuts it into equal parts,
// Transients cuts it at onsets whose strength reaches 1 - sensitivity. The
// first slice always begins at start. Shared by playback and the waveform view.
struct SliceSpec
{
    SliceMode mode = SliceMode::Off;
    int gridCount = 8;
    float sensitivity = 0.5f;
};

int sliceCount (const SampleData& data, const SliceSpec& spec, double start, double end);
std::pair<double, double> sliceRegion (const SampleData& data, const SliceSpec& spec, double start, double end, int index);

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
        SliceSpec slices;
    };

    void prepare (double hostSampleRate);
    void setData (const SampleData* newData);     // once per block
    void setSettings (const Settings& newSettings);
    void trigger (int slice = -1); // slice mode on: -1 plays slice 1
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
    bool sliced = false;
    double sliceStart = 0.0, sliceEnd = 0.0;
};

} // namespace batida
