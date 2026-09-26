#include "PatternGrid.h"

#include "Frame.h"

#include "Look/Theme.h"

using namespace batida;
using namespace theme;

namespace
{
std::optional<Track> trackClipboard;

constexpr float kVelocityPerPixel = 0.8f; // ~160 px from 0 to 127
constexpr int kDragThreshold = 3;


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
    bool changed = version != shownVersion || pattern != shownPattern || proc.getNamesVersion() != shownNames;
    shownNames = proc.getNamesVersion();

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
    g.fillAll (bg);
    const auto& pat = proc.patterns().get().patterns[(size_t) patternIndex()];
    const auto pageStart = page * kStepsPerPage;

    for (int row = 0; row <= kNumTracks; ++row)
    {
        const bool xyRow = row == kNumTracks;
        const auto trackLen = xyRow ? pat.length : std::min (pat.tracks[(size_t) row].length, pat.length);
        const auto playing = shownSteps[(size_t) row];

        for (int i = 0; i < kStepsPerPage; ++i)
        {
            const auto step = pageStart + i;
            const auto cell = cellBounds (row, i).withTrimmedRight (2).withTrimmedBottom (3);
            const bool inPattern = step < pat.length;
            const bool inTrack = step < trackLen;

            auto base = (i / 4) % 2 == 0 ? cellA : cellB;
            if (! inPattern)
                base = bg;
            g.setColour (base);
            g.fillRect (cell);
            if (! inPattern)
            {
                g.setColour (grid);
                g.drawRect (cell, 1);
                continue;
            }
            const auto alpha = inTrack ? 1.0f : 0.3f;

            if (xyRow)
            {
                const auto& l = pat.xy[(size_t) step];
                if (l.active)
                {
                    const auto m = std::min (cell.getWidth(), cell.getHeight()) - 12;
                    const auto box = juce::Rectangle<int> (m, m).withCentre (cell.getCentre()).toFloat();
                    g.setColour (lime);
                    g.drawRect (box, 1.0f);
                    const juce::Point<float> p (box.getX() + l.x * box.getWidth(), box.getBottom() - l.y * box.getHeight());
                    g.setColour (lime.withAlpha (0.4f));
                    g.fillRect (p.x, box.getY(), 1.0f, box.getHeight());
                    g.fillRect (box.getX(), p.y, box.getWidth(), 1.0f);
                    g.setColour (lime);
                    g.fillRect (juce::Rectangle<float> (4.0f, 4.0f).withCentre (p));
                }
            }
            else
            {
                const auto& s = pat.tracks[(size_t) row].steps[(size_t) step];
                if (s.gate)
                {
                    // The bar shows the lane's value; in Steps it is the velocity.
                    const auto v = laneValue (s, lane);
                    const auto inner = cell.reduced (3);
                    const auto bar = inner.withTop (inner.getBottom() - std::max (2, juce::roundToInt (v * (float) inner.getHeight())));
                    g.setColour (ink.withAlpha (alpha));
                    g.fillRect (bar);

                    juce::String marks;
                    if (lane == Lane::Steps)
                    {
                        if (s.ratchet > 1) marks << "x" << (int) s.ratchet;
                        if (s.probability < 100) marks << " " << (int) s.probability << "%";
                    }
                    else
                        marks = laneValueText (s, lane);
                    const auto pressed = pressRow == row && pressStep == step && moved;
                    if (pressed)
                        marks = laneValueText (s, lane);
                    if (marks.isNotEmpty())
                    {
                        g.setColour (pressed ? ink : lime.withAlpha (alpha));
                        g.setFont (mono (8.5f, Weight::Medium));
                        g.drawText (marks.trim(), cell.reduced (4, 3), juce::Justification::topLeft, false);
                    }
                }
            }

            if (step == playing)
            {
                g.setColour (lime);
                g.drawRect (cell, 1);
            }
        }
    }
}

void PatternGrid::trackMenu (int track, juce::Component& target)
{
    const auto pi = patternIndex();
    juce::PopupMenu m;
    juce::PopupMenu swapMenu;
    for (int v = 0; v < kNumTracks; ++v)
        if (v != track)
            swapMenu.addItem (100 + v, juce::String (v + 1) + " " + proc.getVoiceName (v));

    m.addSectionHeader (juce::String (track + 1) + " " + proc.getVoiceName (track));
    m.addItem (7, "Rename...");
    m.addSubMenu ("Swap with", swapMenu);
    m.addSubMenu ("Resample here", resampleMenu (proc, track, pi));
    m.addSeparator();
    m.addItem (1, "Copy track");
    m.addItem (2, "Paste track", trackClipboard.has_value());
    m.addItem (3, "Clear track");
    m.addSeparator();
    m.addItem (4, "Fill every step");
    m.addItem (5, "Fill every 2nd step");
    m.addItem (6, "Fill every 4th step");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                     [safe = juce::Component::SafePointer<PatternGrid> (this), track, pi] (int choice)
    {
        if (safe == nullptr || choice == 0)
            return;
        auto& self = *safe;
        auto& bp = self.proc;
        if (runResample (bp, choice, track, pi))
        {
            self.repaint();
            return;
        }
        if (choice == 7)
        {
            if (self.onRename)
                self.onRename (track);
            return;
        }
        if (choice >= 100)
        {
            bp.swapVoices (track, choice - 100);
            self.repaint();
            return;
        }
        bp.beginUndoStep();
        bp.patterns().edit ([&] (PatternBank& b)
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
        self.repaint();
    });
}

void PatternGrid::mouseDown (const juce::MouseEvent& e)
{
    pressRow = pressStep = -1;
    moved = painting = false;
    const auto pi = patternIndex();

    int row, step;
    if (! hitCell (e.getPosition(), row, step))
        return;
    const auto absStep = page * kStepsPerPage + step;
    const auto& pat = proc.patterns().get().patterns[(size_t) pi];
    if (absStep >= pat.length)
        return;

    proc.beginUndoStep(); // kept only if this press changes something

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
    startTimerHz (15);
}

void PageMap::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const auto& pat = proc.patterns().get().patterns[(size_t) std::clamp (proc.displayPattern(), 0, kNumPatterns - 1)];
    const auto stepW = r.getWidth() / kMaxSteps;
    const auto rowH = (r.getHeight() - 2.0f) / kNumTracks;

    g.fillAll (bg);
    for (int bar = 0; bar < PatternGrid::kPages; ++bar)
    {
        const auto area = juce::Rectangle<float> (r.getX() + bar * 16 * stepW, r.getY(), 16 * stepW, r.getHeight()).reduced (1.0f, 0.0f);
        if (bar * 16 < pat.length)
        {
            g.setColour (theme::cellB);
            g.fillRect (area);
        }
        else
        {
            const float dash[] = { 2.0f, 2.0f };
            g.setColour (theme::faint);
            for (const auto& l : { juce::Line<float> (area.getTopLeft(), area.getTopRight()), juce::Line<float> (area.getBottomLeft(), area.getBottomRight()) })
                g.drawDashedLine (l, dash, 2);
        }
    }

    for (int t = 0; t < kNumTracks; ++t)
        for (int s = 0; s < pat.length; ++s)
            if (pat.tracks[(size_t) t].steps[(size_t) s].gate)
            {
                g.setColour (theme::ink.withAlpha (s < pat.tracks[(size_t) t].length ? 0.9f : 0.3f));
                g.fillRect (r.getX() + s * stepW + 0.5f, r.getY() + 1.0f + t * rowH, std::max (1.0f, stepW - 1.0f), std::max (1.0f, rowH - 0.5f));
            }

    const auto& seq = proc.sequencer();
    if (seq.isRunning() && seq.getPatternStep() >= 0)
    {
        g.setColour (theme::lime);
        g.fillRect (r.getX() + seq.getPatternStep() * stepW, r.getY(), std::max (1.5f, stepW), r.getHeight());
    }

    g.setColour (theme::ink);
    g.drawRect (juce::Rectangle<float> (r.getX() + grid.getPage() * 16 * stepW, r.getY(), 16 * stepW, r.getHeight()), 1.0f);
}

void PageMap::mouseDown (const juce::MouseEvent& e)
{
    const auto bar = (int) (e.x * PatternGrid::kPages / (float) std::max (1, getWidth()));

    if (e.mods.isPopupMenu())
    {
        // Copy or paste one bar (16 steps) of all tracks and the XY locks.
        static std::optional<std::pair<std::array<std::array<Step, 16>, kNumTracks>, std::array<XyLock, 16>>> barClipboard;
        juce::PopupMenu m;
        m.addSectionHeader ("Bar " + juce::String (bar + 1));
        m.addItem (1, "Copy bar");
        m.addItem (2, "Paste bar", barClipboard.has_value());
        m.addItem (3, "Clear bar");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe = juce::Component::SafePointer<PageMap> (this), bar] (int choice)
        {
            if (safe == nullptr || choice == 0) // the editor may have closed while the menu was open
                return;
            auto& bp = safe->proc;
            const auto pi = std::clamp (bp.displayPattern(), 0, kNumPatterns - 1);
            const auto first = bar * 16;
            if (choice == 1)
            {
                const auto& pat = bp.patterns().get().patterns[(size_t) pi];
                std::pair<std::array<std::array<Step, 16>, kNumTracks>, std::array<XyLock, 16>> clip;
                for (int t = 0; t < kNumTracks; ++t)
                    for (int s = 0; s < 16; ++s)
                        clip.first[(size_t) t][(size_t) s] = pat.tracks[(size_t) t].steps[(size_t) (first + s)];
                for (int s = 0; s < 16; ++s)
                    clip.second[(size_t) s] = pat.xy[(size_t) (first + s)];
                barClipboard = clip;
            }
            else if (choice == 2 || choice == 3)
            {
                bp.beginUndoStep();
                bp.patterns().edit ([&] (PatternBank& b)
                {
                    auto& pat = b.patterns[(size_t) pi];
                    for (int t = 0; t < kNumTracks; ++t)
                        for (int s = 0; s < 16; ++s)
                            pat.tracks[(size_t) t].steps[(size_t) (first + s)] = choice == 2 ? barClipboard->first[(size_t) t][(size_t) s] : Step {};
                    for (int s = 0; s < 16; ++s)
                        pat.xy[(size_t) (first + s)] = choice == 2 ? barClipboard->second[(size_t) s] : XyLock {};
                });
            }
            safe->repaint();
        });
        return;
    }

    grid.setPage (bar);
    repaint();
}
