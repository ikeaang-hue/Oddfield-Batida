#include "PatternGrid.h"

using namespace batida;

namespace
{
std::optional<Track> trackClipboard;

juce::String laneValueText (const Step& s, PatternGrid::Lane lane)
{
    switch (lane)
    {
        case PatternGrid::Lane::Velocity:    return juce::String (s.velocity);
        case PatternGrid::Lane::Pitch:       return s.pitch == 0 ? juce::String ("0") : (s.pitch > 0 ? "+" : "") + juce::String (s.pitch);
        case PatternGrid::Lane::Slice:       return juce::String (s.slice + 1);
        case PatternGrid::Lane::Ratchet:     return s.ratchet > 1 ? "x" + juce::String (s.ratchet) : juce::String();
        case PatternGrid::Lane::Probability: return s.probability < 100 ? juce::String (s.probability) : juce::String();
        case PatternGrid::Lane::Gate:        break;
    }
    return {};
}

// 0..1 bar height for the value lanes.
float laneValue (const Step& s, PatternGrid::Lane lane)
{
    switch (lane)
    {
        case PatternGrid::Lane::Velocity:    return s.velocity / 127.0f;
        case PatternGrid::Lane::Pitch:       return (s.pitch + 24) / 48.0f;
        case PatternGrid::Lane::Slice:       return (s.slice + 1) / (float) kMaxSlices;
        case PatternGrid::Lane::Probability: return s.probability / 100.0f;
        case PatternGrid::Lane::Ratchet:     return s.ratchet / (float) kMaxRatchet;
        case PatternGrid::Lane::Gate:        break;
    }
    return 1.0f;
}
} // namespace

PatternGrid::PatternGrid (BatidaProcessor& p) : proc (p)
{
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto& s = chainAmount[(size_t) t];
        s.setSliderStyle (juce::Slider::LinearBar);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setTooltip ("Chain amount for voice " + juce::String (t + 1) + " (0% = dry)");
        s.setColour (juce::Slider::trackColourId, juce::Colour (0xffc0632a).withAlpha (0.6f));
        addAndMakeVisible (s);
        chainAttachments[(size_t) t] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            proc.getState(), voiceParamID (t, vp::ChainAmt), s);
    }
    shownSteps.fill (-2);
    startTimerHz (30);
}

PatternGrid::~PatternGrid()
{
    stopTimer();
}

void PatternGrid::setLane (Lane l)
{
    lane = l;
    repaint();
}

void PatternGrid::setPage (int p)
{
    page = std::clamp (p, 0, kMaxSteps / kStepsPerPage - 1);
    repaint();
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

void PatternGrid::resized()
{
    for (int t = 0; t < kNumTracks; ++t)
        chainAmount[(size_t) t].setBounds (98, t * kRowHeight + 7, 64, kRowHeight - 14);
}

juce::Rectangle<int> PatternGrid::cellBounds (int row, int step) const
{
    const auto w = (float) (getWidth() - kHeaderWidth) / kStepsPerPage;
    const auto x0 = kHeaderWidth + (int) std::round (step * w);
    const auto x1 = kHeaderWidth + (int) std::round ((step + 1) * w);
    return { x0, row * kRowHeight, x1 - x0, kRowHeight };
}

bool PatternGrid::hitCell (juce::Point<int> p, int& row, int& step) const
{
    if (p.x < kHeaderWidth || p.y < 0)
        return false;
    row = p.y / kRowHeight;
    step = (int) ((p.x - kHeaderWidth) * kStepsPerPage / (float) (getWidth() - kHeaderWidth));
    return row >= 0 && row <= kNumTracks && step >= 0 && step < kStepsPerPage;
}

void PatternGrid::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff202326));
    const auto& pat = proc.patterns().get().patterns[(size_t) std::clamp (proc.displayPattern(), 0, kNumPatterns - 1)];
    const auto pageStart = page * kStepsPerPage;

    for (int row = 0; row <= kNumTracks; ++row)
    {
        const bool xyRow = row == kNumTracks;
        const auto header = juce::Rectangle<int> (0, row * kRowHeight, kHeaderWidth, kRowHeight).reduced (4, 2);

        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        if (xyRow)
            g.drawText ("XY", header, juce::Justification::centredLeft);
        else
        {
            const auto& tr = pat.tracks[(size_t) row];
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawFittedText (juce::String (row + 1) + " " + defaultVoiceName (row), header.withWidth (64),
                              juce::Justification::centredLeft, 1, 0.7f);
            g.setFont (juce::FontOptions (11.0f));
            g.setColour (juce::Colours::grey);
            g.drawText (juce::String (std::min (tr.length, pat.length)), header.withX (header.getX() + 64).withWidth (26),
                        juce::Justification::centredRight);
        }

        const auto trackLen = xyRow ? pat.length : std::min (pat.tracks[(size_t) row].length, pat.length);
        const auto playing = shownSteps[(size_t) row];

        for (int i = 0; i < kStepsPerPage; ++i)
        {
            const auto step = pageStart + i;
            const auto cell = cellBounds (row, i).reduced (1);
            const bool inPattern = step < pat.length;
            const bool inTrack = step < trackLen;

            auto base = juce::Colour ((step / 4) % 2 == 0 ? 0xff2d3136 : 0xff272a2e);
            if (! inPattern)
                base = juce::Colour (0xff191b1d);
            else if (! inTrack)
                base = base.darker (0.5f);
            g.setColour (base);
            g.fillRect (cell);

            if (inPattern && step == playing)
            {
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.fillRect (cell);
            }

            if (! inPattern)
                continue;

            if (xyRow)
            {
                const auto& l = pat.xy[(size_t) step];
                if (l.active)
                {
                    g.setColour (juce::Colours::orange);
                    const auto c = cell.toFloat();
                    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ c.getX() + l.x * c.getWidth(),
                                                                                    c.getBottom() - l.y * c.getHeight() }));
                }
                continue;
            }

            const auto& s = pat.tracks[(size_t) row].steps[(size_t) step];
            if (! s.gate)
                continue;

            const auto on = juce::Colour (0xff4fc3f7).withAlpha (inTrack ? 1.0f : 0.35f);
            if (lane == Lane::Gate)
            {
                g.setColour (on.withAlpha ((0.35f + 0.65f * s.velocity / 127.0f) * (inTrack ? 1.0f : 0.35f)));
                g.fillRect (cell.reduced (2));
                if (s.ratchet > 1 || s.probability < 100)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.7f));
                    g.setFont (juce::FontOptions (9.0f));
                    g.drawText ((s.ratchet > 1 ? "x" + juce::String (s.ratchet) : juce::String()) + (s.probability < 100 ? "?" : ""),
                                cell, juce::Justification::centred);
                }
            }
            else
            {
                const auto v = laneValue (s, lane);
                g.setColour (on.withAlpha (0.8f * (inTrack ? 1.0f : 0.35f)));
                g.fillRect (cell.withTop (cell.getBottom() - juce::roundToInt (v * cell.getHeight())));
                g.setColour (juce::Colours::white);
                g.setFont (juce::FontOptions (9.0f));
                g.drawText (laneValueText (s, lane), cell, juce::Justification::centredTop);
            }
        }
    }
}

void PatternGrid::editCell (int row, int step, juce::Point<int> p, bool first)
{
    const auto patternIndex = std::clamp (proc.displayPattern(), 0, kNumPatterns - 1);
    const auto absStep = page * kStepsPerPage + step;
    if (absStep >= proc.patterns().get().patterns[(size_t) patternIndex].length)
        return;

    const auto cell = cellBounds (row, step);
    const auto v = juce::jlimit (0.0f, 1.0f, 1.0f - (float) (p.y - cell.getY()) / (float) cell.getHeight());

    if (row == kNumTracks)
    {
        const auto x = proc.getState().getRawParameterValue (globalParamID (gp::XyX))->load();
        const auto y = proc.getState().getRawParameterValue (globalParamID (gp::XyY))->load();
        proc.patterns().edit ([&] (PatternBank& b) { b.patterns[(size_t) patternIndex].xy[(size_t) absStep] = { true, x, y }; });
        return;
    }

    proc.patterns().edit ([&] (PatternBank& b)
    {
        auto& s = b.patterns[(size_t) patternIndex].tracks[(size_t) row].steps[(size_t) absStep];
        switch (lane)
        {
            case Lane::Gate:
                if (first)
                    paintGate = ! s.gate;
                s.gate = paintGate;
                break;
            case Lane::Velocity:    s.velocity = (uint8_t) juce::jlimit (1, 127, juce::roundToInt (v * 127.0f)); break;
            case Lane::Pitch:       s.pitch = (int8_t) juce::jlimit (-24, 24, juce::roundToInt (v * 48.0f) - 24); break;
            case Lane::Slice:       s.slice = (uint8_t) juce::jlimit (0, kMaxSlices - 1, (int) (v * kMaxSlices)); break;
            case Lane::Probability: s.probability = (uint8_t) juce::jlimit (0, 100, juce::roundToInt (v * 100.0f)); break;
            case Lane::Ratchet:
                if (first)
                    s.ratchet = (uint8_t) (s.ratchet % kMaxRatchet + 1);
                break;
        }
    });
}

void PatternGrid::trackMenu (int track)
{
    const auto patternIndex = std::clamp (proc.displayPattern(), 0, kNumPatterns - 1);
    juce::PopupMenu m;
    m.addSectionHeader ("Track " + juce::String (track + 1));
    m.addItem (1, "Copy");
    m.addItem (2, "Paste", trackClipboard.has_value());
    m.addItem (3, "Clear");
    m.addSeparator();
    m.addItem (4, "Fill every step");
    m.addItem (5, "Fill every 2nd step");
    m.addItem (6, "Fill every 4th step");

    m.showMenuAsync (juce::PopupMenu::Options(), [this, track, patternIndex] (int choice)
    {
        if (choice == 0)
            return;
        proc.patterns().edit ([&] (PatternBank& b)
        {
            auto& tr = b.patterns[(size_t) patternIndex].tracks[(size_t) track];
            switch (choice)
            {
                case 1: trackClipboard = tr; break;
                case 2: if (trackClipboard) tr = *trackClipboard; break;
                case 3: tr = Track {}; tr.length = b.patterns[(size_t) patternIndex].length; break;
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
    const auto patternIndex = std::clamp (proc.displayPattern(), 0, kNumPatterns - 1);

    if (e.x < kHeaderWidth)
    {
        const auto row = e.y / kRowHeight;
        if (row < kNumTracks && e.mods.isPopupMenu())
            trackMenu (row);
        return;
    }

    int row, step;
    if (! hitCell (e.getPosition(), row, step))
        return;
    const auto absStep = page * kStepsPerPage + step;

    if (e.mods.isPopupMenu())
    {
        if (row == kNumTracks)
            proc.patterns().edit ([&] (PatternBank& b) { b.patterns[(size_t) patternIndex].xy[(size_t) absStep] = {}; });
        return;
    }

    if (e.mods.isAltDown() && row < kNumTracks)
    {
        proc.patterns().edit ([&] (PatternBank& b)
        {
            auto& p = b.patterns[(size_t) patternIndex];
            p.tracks[(size_t) row].length = std::clamp (absStep + 1, 1, p.length);
        });
        return;
    }

    lastEditedRow = row;
    lastEditedStep = step;
    editCell (row, step, e.getPosition(), true);
}

void PatternGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.mods.isAltDown() || lane == Lane::Ratchet)
        return;

    int row, step;
    if (! hitCell (e.getPosition(), row, step))
        return;

    // Gate paints across cells; value lanes also follow the height within a cell.
    const auto sameCell = row == lastEditedRow && step == lastEditedStep;
    if (lane == Lane::Gate && sameCell)
        return;
    if (row == kNumTracks && sameCell)
        return;

    lastEditedRow = row;
    lastEditedStep = step;
    editCell (row, step, e.getPosition(), false);
}
