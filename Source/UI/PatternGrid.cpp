#include "PatternGrid.h"

using namespace batida;

namespace
{
std::optional<Track> trackClipboard;

constexpr float kVelocityPerPixel = 0.8f; // ~160 px from 0 to 127
constexpr int kDragThreshold = 3;

const juce::Colour kOn (0xff4fc3f7);

juce::String laneValueText (const Step& s, PatternGrid::Lane lane)
{
    switch (lane)
    {
        case PatternGrid::Lane::Steps:       return juce::String (s.velocity);
        case PatternGrid::Lane::Pitch:       return s.pitch == 0 ? juce::String ("0") : (s.pitch > 0 ? "+" : "") + juce::String (s.pitch);
        case PatternGrid::Lane::Slice:       return juce::String (s.slice + 1);
        case PatternGrid::Lane::Ratchet:     return "x" + juce::String (s.ratchet);
        case PatternGrid::Lane::Probability: return juce::String (s.probability) + "%";
    }
    return {};
}

// 0..1 bar height for each lane.
float laneValue (const Step& s, PatternGrid::Lane lane)
{
    switch (lane)
    {
        case PatternGrid::Lane::Steps:       return s.velocity / 127.0f;
        case PatternGrid::Lane::Pitch:       return (s.pitch + 24) / 48.0f;
        case PatternGrid::Lane::Slice:       return (s.slice + 1) / (float) kMaxSlices;
        case PatternGrid::Lane::Probability: return s.probability / 100.0f;
        case PatternGrid::Lane::Ratchet:     return s.ratchet / (float) kMaxRatchet;
    }
    return 1.0f;
}
} // namespace

// PatternGrid -------------------------------------------------------------------

PatternGrid::PatternGrid (BatidaProcessor& p) : proc (p)
{
    shownSteps.fill (-2);
    startTimerHz (30);
}

PatternGrid::~PatternGrid()
{
    stopTimer();
}

int PatternGrid::patternIndex() const
{
    return std::clamp (proc.displayPattern(), 0, kNumPatterns - 1);
}

void PatternGrid::setLane (Lane l)
{
    lane = l;
    repaint();
}

void PatternGrid::setPage (int p)
{
    page = std::clamp (p, 0, kPages - 1);
    repaint();
    if (onPageChanged)
        onPageChanged();
}

void PatternGrid::timerCallback()
{
    const auto& seq = proc.sequencer();
    const auto version = proc.patterns().getVersion();
    const auto pattern = proc.displayPattern();
    bool changed = version != shownVersion || pattern != shownPattern;

    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto s = seq.isRunning() ? seq.getTrackStep (t) : -1;
        changed |= s != shownSteps[(size_t) t];
        shownSteps[(size_t) t] = s;
    }
    const auto ps = seq.isRunning() ? seq.getPatternStep() : -1;
    changed |= ps != shownSteps[(size_t) kNumTracks];
    shownSteps[(size_t) kNumTracks] = ps;

    shownVersion = version;
    shownPattern = pattern;
    if (changed)
        repaint();
}

juce::Rectangle<int> PatternGrid::cellBounds (int row, int step) const
{
    const auto w = (float) (getWidth() - kHeaderWidth) / kStepsPerPage;
    const auto x0 = kHeaderWidth + (int) std::round (step * w);
    const auto x1 = kHeaderWidth + (int) std::round ((step + 1) * w);
    return { x0, row * rowHeight(), x1 - x0, rowHeight() };
}

bool PatternGrid::hitCell (juce::Point<int> p, int& row, int& step) const
{
    if (p.x < kHeaderWidth || p.y < 0)
        return false;
    row = p.y / std::max (1, rowHeight());
    step = (int) ((p.x - kHeaderWidth) * kStepsPerPage / (float) (getWidth() - kHeaderWidth));
    return row >= 0 && row <= kNumTracks && step >= 0 && step < kStepsPerPage;
}

void PatternGrid::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff202326));
    const auto& pat = proc.patterns().get().patterns[(size_t) patternIndex()];
    const auto pageStart = page * kStepsPerPage;
    const auto rh = rowHeight();

    for (int row = 0; row <= kNumTracks; ++row)
    {
        const bool xyRow = row == kNumTracks;
        const auto header = juce::Rectangle<int> (0, row * rh, kHeaderWidth, rh).reduced (6, 2);

        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        if (xyRow)
            g.drawText ("XY lock", header, juce::Justification::centredLeft);
        else
        {
            const auto& tr = pat.tracks[(size_t) row];
            g.drawFittedText (juce::String (row + 1) + " " + defaultVoiceName (row), header.withTrimmedRight (30),
                              juce::Justification::centredLeft, 1, 0.7f);
            g.setFont (juce::FontOptions (12.0f));
            g.setColour (juce::Colours::grey);
            g.drawText (juce::String (std::min (tr.length, pat.length)), header, juce::Justification::centredRight);
        }

        const auto trackLen = xyRow ? pat.length : std::min (pat.tracks[(size_t) row].length, pat.length);
        const auto playing = shownSteps[(size_t) row];

        for (int i = 0; i < kStepsPerPage; ++i)
        {
            const auto step = pageStart + i;
            const auto cell = cellBounds (row, i).reduced (2);
            const bool inPattern = step < pat.length;
            const bool inTrack = step < trackLen;
            const auto alpha = inTrack ? 1.0f : 0.35f;

            auto base = juce::Colour (i / 4 % 2 == 0 ? 0xff30343a : 0xff292c31);
            if (! inPattern)
                base = juce::Colour (0xff191b1d);
            else if (! inTrack)
                base = base.darker (0.5f);
            g.setColour (base);
            g.fillRoundedRectangle (cell.toFloat(), 3.0f);

            if (inPattern && step == playing)
            {
                g.setColour (juce::Colours::white.withAlpha (0.16f));
                g.fillRoundedRectangle (cell.toFloat(), 3.0f);
            }
            if (! inPattern)
                continue;

            if (xyRow)
            {
                const auto& l = pat.xy[(size_t) step];
                if (l.active)
                {
                    const auto c = cell.toFloat().reduced (4.0f);
                    g.setColour (juce::Colours::orange.withAlpha (0.18f));
                    g.fillRoundedRectangle (c, 2.0f);
                    g.setColour (juce::Colours::orange);
                    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ c.getX() + l.x * c.getWidth(),
                                                                                     c.getBottom() - l.y * c.getHeight() }));
                }
                continue;
            }

            const auto& s = pat.tracks[(size_t) row].steps[(size_t) step];
            if (! s.gate)
                continue;

            // The bar shows the lane's value; in Steps it is the velocity.
            const auto v = laneValue (s, lane);
            const auto bar = cell.withTop (cell.getBottom() - std::max (3, juce::roundToInt (v * cell.getHeight())));
            g.setColour (kOn.withAlpha (0.35f * alpha));
            g.fillRoundedRectangle (cell.toFloat(), 3.0f);
            g.setColour (kOn.withAlpha (alpha));
            g.fillRoundedRectangle (bar.toFloat(), 3.0f);

            juce::String marks;
            if (lane == Lane::Steps)
            {
                if (s.ratchet > 1) marks << "x" << (int) s.ratchet;
                if (s.probability < 100) marks << " " << (int) s.probability << "%";
            }
            else
                marks = laneValueText (s, lane);

            if (marks.isNotEmpty() || (pressRow == row && pressStep == step && moved))
            {
                g.setColour (juce::Colours::white);
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                const auto text = pressRow == row && pressStep == step && moved ? laneValueText (s, lane) : marks.trim();
                g.drawText (text, cell.reduced (3, 2), juce::Justification::centredTop);
            }
        }
    }
}

void PatternGrid::trackMenu (int track)
{
    const auto pi = patternIndex();
    juce::PopupMenu m;
    m.addSectionHeader ("Track " + juce::String (track + 1));
    m.addItem (1, "Copy");
    m.addItem (2, "Paste", trackClipboard.has_value());
    m.addItem (3, "Clear");
    m.addSeparator();
    m.addItem (4, "Fill every step");
    m.addItem (5, "Fill every 2nd step");
    m.addItem (6, "Fill every 4th step");

    m.showMenuAsync (juce::PopupMenu::Options(), [this, track, pi] (int choice)
    {
        if (choice == 0)
            return;
        proc.patterns().edit ([&] (PatternBank& b)
        {
            auto& tr = b.patterns[(size_t) pi].tracks[(size_t) track];
            switch (choice)
            {
                case 1: trackClipboard = tr; break;
                case 2: if (trackClipboard) tr = *trackClipboard; break;
                case 3: tr = Track {}; tr.length = b.patterns[(size_t) pi].length; break;
                case 4: case 5: case 6:
                {
                    const auto every = choice == 4 ? 1 : (choice == 5 ? 2 : 4);
                    for (int s = 0; s < kMaxSteps; ++s)
                        tr.steps[(size_t) s].gate = s % every == 0;
                    break;
                }
                default: break;
            }
        });
        repaint();
    });
}

void PatternGrid::mouseDown (const juce::MouseEvent& e)
{
    pressRow = pressStep = -1;
    moved = painting = false;
    const auto pi = patternIndex();

    if (e.x < kHeaderWidth)
    {
        const auto row = e.y / std::max (1, rowHeight());
        if (row < kNumTracks && e.mods.isPopupMenu())
            trackMenu (row);
        return;
    }

    int row, step;
    if (! hitCell (e.getPosition(), row, step))
        return;
    const auto absStep = page * kStepsPerPage + step;
    const auto& pat = proc.patterns().get().patterns[(size_t) pi];
    if (absStep >= pat.length)
        return;

    if (e.mods.isAltDown() && row < kNumTracks)
    {
        proc.patterns().edit ([&] (PatternBank& b)
        {
            auto& p = b.patterns[(size_t) pi];
            p.tracks[(size_t) row].length = std::clamp (absStep + 1, 1, p.length);
        });
        return;
    }

    if (e.mods.isPopupMenu() && row == kNumTracks)
    {
        proc.patterns().edit ([&] (PatternBank& b) { b.patterns[(size_t) pi].xy[(size_t) absStep] = {}; });
        return;
    }

    pressRow = row;
    pressStep = absStep;
    if (row == kNumTracks)
        startLock = pat.xy[(size_t) absStep];
    else
        startStep = pat.tracks[(size_t) row].steps[(size_t) absStep];

    painting = e.mods.isShiftDown() && row < kNumTracks && lane == Lane::Steps;
    lastPaintStep = -1;
    if (painting)
        mouseDrag (e);
}

void PatternGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (pressRow < 0)
        return;
    const auto pi = patternIndex();

    // Shift-drag paints steps on along the row.
    if (painting)
    {
        int row, step;
        if (! hitCell ({ e.x, pressRow * rowHeight() + 1 }, row, step))
            return;
        const auto absStep = page * kStepsPerPage + step;
        if (absStep == lastPaintStep || absStep >= proc.patterns().get().patterns[(size_t) pi].length)
            return;
        lastPaintStep = absStep;
        moved = true;
        proc.patterns().edit ([&] (PatternBank& b) { b.patterns[(size_t) pi].tracks[(size_t) pressRow].steps[(size_t) absStep].gate = true; });
        return;
    }

    if (! moved && e.getDistanceFromDragStart() < kDragThreshold)
        return;
    moved = true;
    const auto up = (float) -e.getDistanceFromDragStartY();

    if (pressRow == kNumTracks)
    {
        // Move the lock: up/down = heat, left/right = character.
        const auto base = startLock.active ? startLock
                                           : XyLock { true, proc.getState().getRawParameterValue (globalParamID (gp::XyX))->load(),
                                                      proc.getState().getRawParameterValue (globalParamID (gp::XyY))->load() };
        const XyLock l { true, juce::jlimit (0.0f, 1.0f, base.x + e.getDistanceFromDragStartX() / 200.0f),
                         juce::jlimit (0.0f, 1.0f, base.y + up / 200.0f) };
        proc.patterns().edit ([&] (PatternBank& b) { b.patterns[(size_t) pi].xy[(size_t) pressStep] = l; });
        return;
    }

    proc.patterns().edit ([&] (PatternBank& b)
    {
        auto& s = b.patterns[(size_t) pi].tracks[(size_t) pressRow].steps[(size_t) pressStep];
        switch (lane)
        {
            case Lane::Steps:
            {
                // From the step's velocity (or from 0 if it was off); the bottom turns it off.
                const auto v = (startStep.gate ? (float) startStep.velocity : 0.0f) + up * kVelocityPerPixel;
                s.gate = v >= 1.0f;
                if (s.gate)
                    s.velocity = (uint8_t) juce::jlimit (1, 127, juce::roundToInt (v));
                break;
            }
            case Lane::Pitch:       s.pitch = (int8_t) juce::jlimit (-24, 24, startStep.pitch + juce::roundToInt (up / 6.0f)); break;
            case Lane::Slice:       s.slice = (uint8_t) juce::jlimit (0, kMaxSlices - 1, startStep.slice + juce::roundToInt (up / 8.0f)); break;
            case Lane::Ratchet:     s.ratchet = (uint8_t) juce::jlimit (1, kMaxRatchet, startStep.ratchet + juce::roundToInt (up / 16.0f)); break;
            case Lane::Probability: s.probability = (uint8_t) juce::jlimit (0, 100, startStep.probability + juce::roundToInt (up / 2.0f)); break;
        }
    });
    repaint();
}

void PatternGrid::mouseUp (const juce::MouseEvent&)
{
    const auto pi = patternIndex();

    if (pressRow >= 0 && ! moved && ! painting)
    {
        // A tap.
        if (pressRow == kNumTracks)
        {
            const auto x = proc.getState().getRawParameterValue (globalParamID (gp::XyX))->load();
            const auto y = proc.getState().getRawParameterValue (globalParamID (gp::XyY))->load();
            proc.patterns().edit ([&] (PatternBank& b)
            {
                auto& l = b.patterns[(size_t) pi].xy[(size_t) pressStep];
                l = l.active ? XyLock {} : XyLock { true, x, y };
            });
        }
        else if (lane == Lane::Steps)
            proc.patterns().edit ([&] (PatternBank& b)
            {
                auto& s = b.patterns[(size_t) pi].tracks[(size_t) pressRow].steps[(size_t) pressStep];
                s.gate = ! s.gate;
            });
        else if (lane == Lane::Ratchet)
            proc.patterns().edit ([&] (PatternBank& b)
            {
                auto& s = b.patterns[(size_t) pi].tracks[(size_t) pressRow].steps[(size_t) pressStep];
                s.ratchet = (uint8_t) (s.ratchet % kMaxRatchet + 1);
            });
    }

    pressRow = pressStep = -1;
    moved = painting = false;
    repaint();
}

// PageMap -----------------------------------------------------------------------

PageMap::PageMap (BatidaProcessor& p, PatternGrid& g) : proc (p), grid (g)
{
    setTooltip ("All 64 steps: click a bar to show it");
    startTimerHz (15);
}

void PageMap::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto& pat = proc.patterns().get().patterns[(size_t) std::clamp (proc.displayPattern(), 0, kNumPatterns - 1)];
    const auto stepW = r.getWidth() / kMaxSteps;
    const auto rowH = r.getHeight() / kNumTracks;

    g.setColour (juce::Colour (0xff191b1d));
    g.fillRoundedRectangle (r, 3.0f);

    for (int bar = 0; bar < PatternGrid::kPages; ++bar)
    {
        const auto area = juce::Rectangle<float> (r.getX() + bar * 16 * stepW, r.getY(), 16 * stepW, r.getHeight());
        const bool used = bar * 16 < pat.length;
        g.setColour (used ? juce::Colour (0xff2d3136) : juce::Colour (0xff1c1e21));
        g.fillRect (area.reduced (1.0f, 0.0f));
    }

    for (int t = 0; t < kNumTracks; ++t)
        for (int s = 0; s < pat.length; ++s)
            if (pat.tracks[(size_t) t].steps[(size_t) s].gate)
            {
                g.setColour (kOn.withAlpha (s < pat.tracks[(size_t) t].length ? 0.9f : 0.3f));
                g.fillRect (r.getX() + s * stepW + 0.5f, r.getY() + t * rowH + 0.5f, std::max (1.0f, stepW - 1.0f),
                            std::max (1.0f, rowH - 1.0f));
            }

    const auto& seq = proc.sequencer();
    if (seq.isRunning() && seq.getPatternStep() >= 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.fillRect (r.getX() + seq.getPatternStep() * stepW, r.getY(), std::max (1.5f, stepW), r.getHeight());
    }

    g.setColour (juce::Colours::orange);
    g.drawRoundedRectangle (juce::Rectangle<float> (r.getX() + grid.getPage() * 16 * stepW, r.getY(), 16 * stepW, r.getHeight()),
                            2.0f, 2.0f);
}

void PageMap::mouseDown (const juce::MouseEvent& e)
{
    const auto bar = (int) (e.x * PatternGrid::kPages / (float) std::max (1, getWidth()));
    grid.setPage (bar);
    repaint();
}
