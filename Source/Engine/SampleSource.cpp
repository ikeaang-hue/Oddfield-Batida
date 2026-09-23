#include "SampleSource.h"

#include <algorithm>
#include <cmath>

namespace batida
{

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

void SampleSource::trigger()
{
    if (data == nullptr || data->numFrames() < 2)
    {
        playing = false;
        return;
    }

    const auto len = (double) data->numFrames();
    const auto s = std::min (settings.start, settings.end) * len;
    const auto e = std::max (settings.start, settings.end) * len;

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
    const auto s = std::min (settings.start, settings.end) * len;
    const auto e = std::max (settings.start, settings.end) * len;

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
