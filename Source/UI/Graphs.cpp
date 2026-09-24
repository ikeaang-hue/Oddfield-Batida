#include "Graphs.h"

#include "Engine/FmSource.h"

using namespace theme;

namespace
{
constexpr float kInset = 5.0f;

void strokeCurve (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width = 1.4f)
{
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void fillUnder (juce::Graphics& g, juce::Path p, juce::Rectangle<float> r, float fromX, float toX, juce::Colour c)
{
    p.lineTo (toX, r.getBottom());
    p.lineTo (fromX, r.getBottom());
    p.closeSubPath();
    g.setColour (c);
    g.fillPath (p);
}

// The voice drive's transfer curves (Engine/Processors.cpp, Drive::shape).
float driveShape (float x, float amount, int type)
{
    switch (type)
    {
        case 0:  return std::tanh ((1.0f + 24.0f * amount) * x) * (1.0f - 0.4f * amount);
        case 1:  return std::clamp ((1.0f + 24.0f * amount) * x, -1.0f, 1.0f) * (1.0f - 0.4f * amount);
        case 2:  return std::sin (1.570796327f * (1.0f + 8.0f * amount) * x) * (1.0f - 0.3f * amount);
        default: break;
    }
    const auto levels = std::exp2 (16.0f - 13.0f * amount); // crush: quantised
    return std::round (x * levels) / levels;
}

// An FM operator's wave (Engine/FmSource.cpp, shapeWave): sine → triangle →
// saw → square → noise.
float fmWave (float p, float morph, float noise)
{
    auto basic = [&] (int shape)
    {
        switch (shape)
        {
            case 0:  return std::sin (6.2831853f * p);
            case 1:  return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
            case 2:  return p < 0.5f ? 2.0f * p : 2.0f * p - 2.0f;
            case 3:  return p < 0.5f ? 1.0f : -1.0f;
            default: return noise;
        }
    };
    const auto m = morph * 4.0f;
    const auto i = std::min ((int) m, 3);
    const auto t = m - (float) i;
    auto w = basic (i);
    if (t > 0.0f)
        w += (basic (i + 1) - w) * t;
    return w;
}

// Level of an exponential fall that reaches −60 dB after `ms`.
float fall (float t, float ms)
{
    return ms <= 0.0f ? 0.0f : std::exp (-6.9f * t / ms);
}
} // namespace

void drawGraphFrame (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (bg);
    g.fillRect (r);
    g.setColour (line);
    g.drawRect (r, 1.0f);
}

// AmpEnvelopeGraph -----------------------------------------------------------------

void AmpEnvelopeGraph::set (float a, float d, float s, float r, bool gate)
{
    const std::array<float, 5> v { a, d, s, r, gate ? 1.0f : 0.0f };
    if (v != values)
    {
        values = v;
        repaint();
    }
}

void AmpEnvelopeGraph::paint (juce::Graphics& g)
{
    const auto frame = getLocalBounds().toFloat();
    drawGraphFrame (g, frame);
    const auto r = frame.reduced (kInset);
    const auto [a, d, s, rel, gateFlag] = values;
    const auto gate = gateFlag > 0.5f;

    // One-shot: attack, then decay to sustain, then the release from there.
    // Gate: the sustain holds for a while before the release.
    const auto hold = gate ? std::max (80.0f, 0.35f * (a + d)) : 0.0f;
    const auto decayShown = s > 0.001f ? std::min (d, 1.5f * d) : d;
    const auto total = std::max (1.0f, a + decayShown + hold + (s > 0.001f || gate ? rel : 0.0f));
    auto xAt = [&] (float t) { return r.getX() + t / total * r.getWidth(); };
    auto yAt = [&] (float level) { return r.getBottom() - level * r.getHeight(); };

    juce::Path p;
    p.startNewSubPath (xAt (0.0f), yAt (0.0f));
    p.lineTo (xAt (a), yAt (1.0f));
    constexpr int steps = 60;
    for (int i = 1; i <= steps; ++i)
    {
        const auto t = decayShown * (float) i / steps;
        p.lineTo (xAt (a + t), yAt (s + (1.0f - s) * fall (t, d)));
    }
    const auto sustainLevel = s + (1.0f - s) * fall (decayShown, d);
    if (gate)
        p.lineTo (xAt (a + decayShown + hold), yAt (sustainLevel));
    if (s > 0.001f || gate)
        for (int i = 1; i <= steps; ++i)
        {
            const auto t = rel * (float) i / steps;
            p.lineTo (xAt (a + decayShown + hold + t), yAt (sustainLevel * fall (t, rel)));
        }

    fillUnder (g, p, r, r.getX(), xAt (total), lime.withAlpha (0.08f));
    strokeCurve (g, p, lime);

    // Handles: the peak and where the decay ends.
    g.setColour (lime);
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ xAt (a), yAt (1.0f) }));
    g.setColour (bg);
    const auto end = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ xAt (a + decayShown), yAt (sustainLevel) });
    g.fillEllipse (end);
    g.setColour (lime);
    g.drawEllipse (end, 1.3f);
}

// PitchEnvelopeGraph ---------------------------------------------------------------

void PitchEnvelopeGraph::set (float semitones, float decayMs)
{
    if (! juce::exactlyEqual (semitones, amount) || ! juce::exactlyEqual (decayMs, decay))
    {
        amount = semitones;
        decay = decayMs;
        repaint();
    }
}

void PitchEnvelopeGraph::paint (juce::Graphics& g)
{
    const auto frame = getLocalBounds().toFloat();
    drawGraphFrame (g, frame);
    const auto r = frame.reduced (kInset);
    const auto mid = amount >= 0.0f ? r.getBottom() : r.getY(); // the resting pitch
    const auto depth = juce::jlimit (0.0f, 1.0f, std::abs (amount) / 48.0f) * 0.85f + (std::abs (amount) > 1.0e-4f ? 0.15f : 0.0f);
    const auto span = std::max (100.0f, 2.5f * decay);

    g.setColour (grid);
    g.fillRect (r.getX(), mid - 0.5f, r.getWidth(), 1.0f);

    juce::Path p;
    constexpr int steps = 60;
    for (int i = 0; i <= steps; ++i)
    {
        const auto t = span * (float) i / steps;
        const auto level = depth * std::exp (-4.6f * t / std::max (1.0f, decay));
        const auto x = r.getX() + (float) i / steps * r.getWidth();
        const auto y = amount >= 0.0f ? mid - level * r.getHeight() : mid + level * r.getHeight();
        i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
    }
    strokeCurve (g, p, lime);
}

// MiniEnvelope ---------------------------------------------------------------------

void MiniEnvelope::set (float a, float d, float s, float r, bool dim)
{
    const std::array<float, 4> v { a, d, s, r };
    if (v != values || dim != dimmed)
    {
        values = v;
        dimmed = dim;
        repaint();
    }
}

void MiniEnvelope::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f, 2.0f);
    const auto [a, d, s, rel] = values;
    g.setColour (line);
    g.fillRect (r.getX(), r.getBottom(), r.getWidth(), 1.0f);

    // The first second: attack, decay to sustain, a short hold, then the release.
    constexpr float span = 1000.0f;
    auto xAt = [&] (float t) { return r.getX() + std::min (t, span) / span * r.getWidth(); };
    auto yAt = [&] (float level) { return r.getBottom() - level * r.getHeight(); };
    juce::Path p;
    p.startNewSubPath (xAt (0.0f), yAt (0.0f));
    p.lineTo (xAt (a), yAt (1.0f));
    const auto decayEnd = std::min (span, a + (s > 0.001f ? std::min (d, 300.0f) : d));
    for (int i = 1; i <= 24; ++i)
    {
        const auto t = (decayEnd - a) * (float) i / 24.0f;
        p.lineTo (xAt (a + t), yAt (s + (1.0f - s) * fall (t, d)));
    }
    if (s > 0.001f && decayEnd < span)
    {
        const auto holdEnd = std::max (decayEnd, 0.7f * span);
        p.lineTo (xAt (holdEnd), yAt (s));
        for (int i = 1; i <= 12; ++i)
        {
            const auto t = (span - holdEnd) * (float) i / 12.0f;
            p.lineTo (xAt (holdEnd + t), yAt (s * fall (t, rel)));
        }
    }
    strokeCurve (g, p, dimmed ? faint : ink, 1.1f);
}

// FilterGraph ----------------------------------------------------------------------

void FilterGraph::set (int type, float cutoffHz, float resonance)
{
    if (type != filterType || ! juce::exactlyEqual (cutoffHz, cutoff) || ! juce::exactlyEqual (resonance, res))
    {
        filterType = type;
        cutoff = cutoffHz;
        res = resonance;
        repaint();
    }
}

void FilterGraph::paint (juce::Graphics& g)
{
    const auto frame = getLocalBounds().toFloat();
    drawGraphFrame (g, frame);
    const auto r = frame.reduced (kInset);

    for (const auto f : { 100.0f, 1000.0f, 10000.0f })
    {
        const auto x = r.getX() + std::log10 (f / 20.0f) / 3.0f * r.getWidth();
        g.setColour (grid);
        g.fillRect (x, frame.getY() + 1.0f, 1.0f, frame.getHeight() - 2.0f);
    }

    // A 2-pole response: magnitude of the analog prototype at each frequency.
    const auto q = 0.707f + res * 9.0f;
    auto magnitudeDb = [&] (float f)
    {
        const auto w = f / std::max (10.0f, cutoff);
        const auto re = 1.0f - w * w, im = w / q;
        const auto den = std::sqrt (re * re + im * im);
        float num = 1.0f;
        if (filterType == 1)
            num = w * w;
        else if (filterType == 2)
            num = w / q;
        return 20.0f * std::log10 (std::max (1.0e-5f, num / den));
    };
    constexpr float topDb = 18.0f, bottomDb = -30.0f;
    auto yOf = [&] (float db) { return r.getY() + (topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb) * r.getHeight(); };

    g.setColour (grid);
    g.fillRect (r.getX(), yOf (0.0f) - 0.5f, r.getWidth(), 1.0f);

    juce::Path p;
    constexpr int steps = 120;
    for (int i = 0; i <= steps; ++i)
    {
        const auto f = 20.0f * std::pow (1000.0f, (float) i / steps);
        const auto x = r.getX() + (float) i / steps * r.getWidth();
        i == 0 ? p.startNewSubPath (x, yOf (magnitudeDb (f))) : p.lineTo (x, yOf (magnitudeDb (f)));
    }
    strokeCurve (g, p, lime);

    // The cutoff handle.
    const auto cx = r.getX() + juce::jlimit (0.0f, 1.0f, std::log10 (std::max (20.0f, cutoff) / 20.0f) / 3.0f) * r.getWidth();
    const auto h = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ cx, yOf (magnitudeDb (cutoff)) });
    g.setColour (bg);
    g.fillEllipse (h);
    g.setColour (lime);
    g.drawEllipse (h, 1.3f);
}

// DriveGraph -----------------------------------------------------------------------

void DriveGraph::set (int type, float amount)
{
    if (type != driveType || ! juce::exactlyEqual (amount, drive))
    {
        driveType = type;
        drive = amount;
        repaint();
    }
}

void DriveGraph::paint (juce::Graphics& g)
{
    const auto frame = getLocalBounds().toFloat();
    drawGraphFrame (g, frame);
    const auto r = frame.reduced (kInset);
    g.setColour (grid);
    g.fillRect (r.getX(), r.getCentreY() - 0.5f, r.getWidth(), 1.0f);
    g.fillRect (r.getCentreX() - 0.5f, r.getY(), 1.0f, r.getHeight());

    juce::Path diagonal;
    diagonal.startNewSubPath (r.getX(), r.getBottom());
    diagonal.lineTo (r.getRight(), r.getY());
    g.strokePath (diagonal, juce::PathStrokeType (1.0f));

    juce::Path p;
    constexpr int steps = 120;
    const auto type = juce::jlimit (0, 3, driveType);
    for (int i = 0; i <= steps; ++i)
    {
        const auto x = -1.0f + 2.0f * (float) i / steps;
        const auto y = drive <= 0.0f ? x : driveShape (x, drive, type);
        const auto px = r.getX() + (x + 1.0f) * 0.5f * r.getWidth();
        const auto py = r.getCentreY() - juce::jlimit (-1.0f, 1.0f, y) * r.getHeight() * 0.5f;
        i == 0 ? p.startNewSubPath (px, py) : p.lineTo (px, py);
    }
    strokeCurve (g, p, lime);
}

// AlgorithmView --------------------------------------------------------------------

void AlgorithmView::set (int index)
{
    if (index != algorithm)
    {
        algorithm = index;
        repaint();
    }
}

void AlgorithmView::paint (juce::Graphics& g)
{
    const auto frame = getLocalBounds().toFloat();
    drawGraphFrame (g, frame);
    const auto& alg = batida::algorithms()[(size_t) juce::jlimit (0, batida::kNumAlgorithms - 1, algorithm)];
    const auto area = frame.reduced (10.0f, 8.0f).withTrimmedTop (10.0f);

    g.setColour (muted);
    g.setFont (mono (8.5f));
    g.drawText ("ALG " + juce::String (algorithm + 1), frame.reduced (6.0f, 4.0f), juce::Justification::topLeft);

    // Row 0 = carriers; a modulator sits one row above the highest op it feeds.
    std::array<int, batida::kNumOps> row {};
    for (int k = 0; k < batida::kNumOps; ++k)
    {
        if (alg.isCarrier (k))
            continue;
        for (int j = 0; j < k; ++j)
            if ((alg.modulators[(size_t) j] >> k) & 1)
                row[(size_t) k] = std::max (row[(size_t) k], row[(size_t) j] + 1);
    }
    const auto rows = *std::max_element (row.begin(), row.end()) + 1;
    const auto box = std::min (24.0f, area.getHeight() / (float) (rows + 1) - 4.0f);
    const auto outY = area.getBottom() - 4.0f;
    const auto rowStep = std::min (box + 14.0f, (outY - area.getY() - box - 8.0f) / (float) std::max (1, rows - 1));

    std::array<juce::Rectangle<float>, batida::kNumOps> boxes;
    for (int rr = 0; rr < rows; ++rr)
    {
        std::vector<int> inRow;
        for (int k = 0; k < batida::kNumOps; ++k)
            if (row[(size_t) k] == rr)
                inRow.push_back (k);
        const auto y = outY - 10.0f - box - (float) rr * rowStep;
        for (size_t i = 0; i < inRow.size(); ++i)
        {
            const auto cx = area.getX() + area.getWidth() * ((float) i + 0.5f) / (float) inRow.size();
            boxes[(size_t) inRow[i]] = { cx - box / 2.0f, y, box, box };
        }
    }

    const auto modColour = muted;
    for (int k = 0; k < batida::kNumOps; ++k)
    {
        for (int j = 0; j < k; ++j)
            if ((alg.modulators[(size_t) j] >> k) & 1)
            {
                g.setColour (modColour);
                g.drawArrow ({ boxes[(size_t) k].getCentreX(), boxes[(size_t) k].getBottom(), boxes[(size_t) j].getCentreX(),
                               boxes[(size_t) j].getY() },
                             1.1f, 5.0f, 5.0f);
            }
        if (alg.isCarrier (k))
        {
            g.setColour (lime);
            g.drawLine (boxes[(size_t) k].getCentreX(), boxes[(size_t) k].getBottom(), boxes[(size_t) k].getCentreX(), outY, 1.1f);
        }
    }
    float minX = 1.0e9f, maxX = -1.0e9f;
    for (int k = 0; k < batida::kNumOps; ++k)
        if (alg.isCarrier (k))
        {
            minX = std::min (minX, boxes[(size_t) k].getCentreX());
            maxX = std::max (maxX, boxes[(size_t) k].getCentreX());
        }
    g.setColour (lime);
    g.drawLine (minX - 3.0f, outY, maxX + 3.0f, outY, 1.1f);

    // Feedback loop on op 4.
    const auto fb = boxes[3];
    juce::Path loop;
    loop.startNewSubPath (fb.getRight(), fb.getCentreY());
    loop.lineTo (fb.getRight() + 6.0f, fb.getCentreY());
    loop.lineTo (fb.getRight() + 6.0f, fb.getY() - 5.0f);
    loop.lineTo (fb.getCentreX(), fb.getY() - 5.0f);
    loop.lineTo (fb.getCentreX(), fb.getY());
    g.setColour (alg.isCarrier (3) ? lime : modColour);
    g.strokePath (loop, juce::PathStrokeType (1.1f));

    for (int k = 0; k < batida::kNumOps; ++k)
    {
        const auto carrier = alg.isCarrier (k);
        if (carrier)
        {
            g.setColour (lime);
            g.fillRect (boxes[(size_t) k]);
        }
        else
        {
            g.setColour (bg);
            g.fillRect (boxes[(size_t) k]);
            g.setColour (modColour);
            g.drawRect (boxes[(size_t) k], 1.1f);
        }
        g.setColour (carrier ? bg : modColour);
        g.setFont (mono (10.5f, Weight::Bold));
        g.drawText (juce::String (k + 1), boxes[(size_t) k], juce::Justification::centred);
    }
}

// PeaksView ------------------------------------------------------------------------

void PeaksView::set (std::vector<float> p, juce::Colour c)
{
    peaks = std::move (p);
    colour = c;
    repaint();
}

void PeaksView::setPlayhead (float position)
{
    if (! juce::exactlyEqual (position, playhead))
    {
        playhead = position;
        repaint();
    }
}

void PeaksView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.fillAll (bg);
    g.setColour (grid);
    g.fillRect (r.getX(), r.getCentreY() - 0.5f, r.getWidth(), 1.0f);
    if (peaks.empty())
        return;

    float top = 1.0e-6f;
    for (const auto p : peaks)
        top = std::max (top, p);
    const auto n = (int) peaks.size();
    const auto half = r.getHeight() * 0.5f - 2.0f;
    g.setColour (colour);
    for (int i = 0; i < n; ++i)
    {
        const auto x = r.getX() + 2.0f + (float) i / (float) n * (r.getWidth() - 4.0f);
        const auto a = peaks[(size_t) i] / top * half;
        g.fillRect (x, r.getCentreY() - a, 1.0f, std::max (1.0f, 2.0f * a));
    }
    if (playhead >= 0.0f)
    {
        g.setColour (ink);
        g.fillRect (r.getX() + playhead * r.getWidth(), r.getY(), 1.0f, r.getHeight());
    }
}

// WaveGlyph ------------------------------------------------------------------------

void WaveGlyph::set (float wave)
{
    if (! juce::exactlyEqual (wave, value))
    {
        value = wave;
        repaint();
    }
}

void WaveGlyph::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    juce::Path p;
    juce::Random random (7);
    constexpr int steps = 48;
    for (int i = 0; i <= steps; ++i)
    {
        const auto phase = (float) i / steps;
        const auto y = fmWave (phase, juce::jlimit (0.0f, 1.0f, value), random.nextFloat() * 2.0f - 1.0f);
        const auto px = r.getX() + phase * r.getWidth();
        const auto py = r.getCentreY() - juce::jlimit (-1.0f, 1.0f, y) * r.getHeight() * 0.45f;
        i == 0 ? p.startNewSubPath (px, py) : p.lineTo (px, py);
    }
    g.setColour (ink);
    g.strokePath (p, juce::PathStrokeType (1.1f));
}

juce::String waveName (float wave)
{
    static const char* names[] = { "Sine", "Tri", "Saw", "Square", "Noise" };
    const auto pos = juce::jlimit (0.0f, 1.0f, wave) * 4.0f;
    const auto i = (int) std::round (pos);
    if (std::abs (pos - (float) i) < 0.08f)
        return names[i];
    const auto lo = (int) std::floor (pos);
    return juce::String (names[lo]) + ">" + names[std::min (4, lo + 1)];
}
