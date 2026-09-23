#include "SampleSource.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace batida
{

int sliceCount (const SampleData& data, const SliceSpec& spec, double start, double end)
{
    if (spec.mode == SliceMode::Grid)
        return std::clamp (spec.gridCount, 2, 32);
    if (spec.mode == SliceMode::Off)
        return 1;

    const auto threshold = 1.0f - std::clamp (spec.sensitivity, 0.0f, 1.0f);
    int n = 1;
    for (const auto& [frame, strength] : data.onsets)
        if (frame > start + 64 && frame < end - 64 && strength >= threshold && n < 32)
            ++n;
    return n;
}

std::pair<double, double> sliceRegion (const SampleData& data, const SliceSpec& spec, double start, double end, int index)
{
    const auto count = sliceCount (data, spec, start, end);
    const auto k = ((index % count) + count) % count;

    if (spec.mode == SliceMode::Grid)
        return { start + (end - start) * k / count, start + (end - start) * (k + 1) / count };
    if (spec.mode == SliceMode::Off)
        return { start, end };

    const auto threshold = 1.0f - std::clamp (spec.sensitivity, 0.0f, 1.0f);
    double from = start, to = end;
    int n = 0;
    for (const auto& [frame, strength] : data.onsets)
    {
        if (frame <= start + 64 || frame >= end - 64 || strength < threshold || n >= 31)
            continue;
        ++n;
        if (n == k)
            from = frame;
        else if (n == k + 1)
        {
            to = frame;
            break;
        }
    }
    return { from, to };
}

void SampleSource::prepare (double hostSampleRate)
{
    hostRate = hostSampleRate;
    reset();
}

void SampleSource::setData (const SampleData* newData)
{
    if (newData != data)
    {
        data = newData;
        playing = false;
    }
}

void SampleSource::setSettings (const Settings& newSettings)
{
    settings = newSettings;
}

void SampleSource::trigger (int slice)
{
    if (data == nullptr || data->numFrames() < 2)
    {
        playing = false;
        return;
    }

    const auto len = (double) data->numFrames();
    auto s = std::min (settings.start, settings.end) * len;
    auto e = std::max (settings.start, settings.end) * len;

    sliced = settings.slices.mode != SliceMode::Off;
    if (sliced)
    {
        std::tie (s, e) = sliceRegion (*data, settings.slices, s, e, std::max (0, slice));
        sliceStart = s;
        sliceEnd = e;
    }

    pos = settings.reverse ? std::max (s, e - 1.0) : s;
    elapsed = 0.0;
    playing = e - s >= 2.0;
}

void SampleSource::reset()
{
    playing = false;
    pos = 0.0;
    elapsed = 0.0;
}

float SampleSource::read (int channel, double position) const
{
    const auto* x = data->audio.getReadPointer (std::min (channel, data->numChannels() - 1));
    const auto last = data->numFrames() - 1;
    const auto i = (int) std::floor (position);
    const auto f = (float) (position - i);

    auto at = [&] (int n) { return x[std::clamp (n, 0, last)]; };
    const auto x0 = at (i - 1), x1 = at (i), x2 = at (i + 1), x3 = at (i + 2);

    // 4-point Hermite
    const auto c1 = 0.5f * (x2 - x0);
    const auto c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const auto c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    return ((c3 * f + c2) * f + c1) * f + x1;
}

void SampleSource::process (float pitchRatio, float& left, float& right)
{
    left = right = 0.0f;
    if (! playing || data == nullptr)
        return;

    const auto len = (double) data->numFrames();
    const auto s = sliced ? sliceStart : std::min (settings.start, settings.end) * len;
    const auto e = sliced ? sliceEnd : std::max (settings.start, settings.end) * len;

    const auto speed = (double) pitchRatio * data->sampleRate / hostRate;
    const auto inc = settings.reverse ? -speed : speed;

    auto ls = std::clamp ((double) std::min (settings.loopStart, settings.loopEnd) * len, s, e);
    auto le = std::clamp ((double) std::max (settings.loopStart, settings.loopEnd) * len, s, e);
    const auto looping = settings.gate && settings.loop && le - ls >= 2.0;

    auto gain = settings.gain;

    if (settings.fadeInMs > 0.0f)
        gain *= (float) std::min (1.0, elapsed / (settings.fadeInMs * 0.001 * hostRate));

    if (settings.fadeOutMs > 0.0f && ! looping)
    {
        const auto remaining = settings.reverse ? pos - s : e - pos;
        const auto samplesLeft = remaining / std::max (speed, 1.0e-6);
        gain *= (float) std::clamp (samplesLeft / (settings.fadeOutMs * 0.001 * hostRate), 0.0, 1.0);
    }

    left = read (0, pos) * gain;
    right = data->numChannels() > 1 ? read (1, pos) * gain : left;

    pos += inc;
    elapsed += 1.0;

    if (looping)
    {
        if (! settings.reverse && pos >= le)
            pos -= le - ls;
        else if (settings.reverse && pos < ls)
            pos += le - ls;
    }
    else if ((! settings.reverse && pos >= e) || (settings.reverse && pos < s))
    {
        playing = false;
    }
}

} // namespace batida
