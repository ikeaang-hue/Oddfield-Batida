#include "PatternVaryOverlay.h"

using namespace batida;
using namespace theme;

// PatternTile ----------------------------------------------------------------------

void PatternTile::set (const juce::String& n, const juce::String& text, const Pattern* p, const Pattern& b, bool isOriginal)
{
    name = n;
    note = text;
    pattern = p != nullptr ? std::optional<Pattern> (*p) : std::nullopt;
    base = b;
    original = isOriginal;
    repaint();
}

void PatternTile::setKept (bool k)
{
    kept = k;
    repaint();
}

void PatternTile::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds();
    const auto available = pattern.has_value();
    if (kept)
        g.fillAll (ink);
    else if (held)
        g.fillAll (raised);
    g.setColour (held ? lime : (isMouseOver() && available ? faint : line));
    g.drawRect (r, 1);

    const auto fg = kept ? bg : ink;
    auto top = r.reduced (8, 6).withHeight (12);
    g.setColour (available ? fg : faint);
    g.setFont (mono (10.0f, Weight::Bold));
    g.drawText (name, top, juce::Justification::centredLeft, false);
    if (available)
    {
        g.setColour (kept ? bg : held ? lime : muted);
        g.setFont (mono (8.5f));
        g.drawText (held ? juce::String::fromUTF8 ("\xe2\x96\xb6 PLAYING") : juce::String::fromUTF8 ("\xe2\x96\xb6 HOLD"), top,
                    juce::Justification::centredRight, false);
    }

    // The grid: 8 rows, one column per step.
    if (available)
    {
        const auto area = r.reduced (8).withTrimmedTop (18).withHeight (54).toFloat();
        const auto len = std::max (1, pattern->length);
        const auto cw = area.getWidth() / (float) len, rh = area.getHeight() / (float) kNumTracks;
        for (int t = 0; t < kNumTracks; ++t)
            for (int s = 0; s < len; ++s)
            {
                const auto& st = pattern->tracks[(size_t) t].steps[(size_t) s];
                const auto& was = base.tracks[(size_t) t].steps[(size_t) s];
                const juce::Rectangle<float> cell (area.getX() + (float) s * cw, area.getY() + (float) t * rh,
                                                   std::max (1.0f, cw - 1.0f), std::max (1.0f, rh - 1.0f));
                if (st.gate)
                {
                    const auto changed = ! original && (! was.gate || was.ratchet != st.ratchet);
                    g.setColour (kept ? bg : changed ? lime : ink.withAlpha (0.35f + 0.65f * st.velocity / 127.0f));
                    g.fillRect (cell);
                }
                else if (was.gate && ! original)
                {
                    g.setColour (kept ? bg : faint);
                    g.drawRect (cell, 1.0f);
                }
                else if (s % 4 == 0)
                {
                    g.setColour (kept ? bg.withAlpha (0.2f) : line);
                    g.fillRect (cell.withWidth (1.0f));
                }
            }
    }

    g.setColour (kept ? bg : muted);
    g.setFont (mono (9.5f));
    g.drawFittedText (note, r.reduced (8, 6).withTop (r.getY() + 80), juce::Justification::topLeft, 2, 1.0f);
}

void PatternTile::mouseDown (const juce::MouseEvent&)
{
    if (! pattern)
        return;
    held = true;
    if (onPress)
        onPress();
    repaint();
}

void PatternTile::mouseUp (const juce::MouseEvent&)
{
    if (! held)
        return;
    held = false;
    if (onRelease)
        onRelease();
    repaint();
}

// PatternVaryOverlay ---------------------------------------------------------------

PatternVaryOverlay::PatternVaryOverlay (BatidaProcessor& p) : proc (p)
{
    for (juce::Component* c : { (juce::Component*) &amount, (juce::Component*) &direction, (juce::Component*) &again,
                                (juce::Component*) &keep, (juce::Component*) &back, (juce::Component*) &save })
        addAndMakeVisible (c);
    again.withDice = true;
    again.onClick = [this] { startVary(); };

    for (int t = 0; t < kNumTracks; ++t)
        addAndMakeVisible (trackLocks.add (new juce::ToggleButton (juce::String (t + 1))));

    for (int i = 0; i <= 4; ++i)
    {
        auto* tile = tiles.add (new PatternTile());
        tile->onPress = [this, i]
        {
            proc.previewPatternCandidate (i - 1);
            proc.auditionPattern (pattern, true);
            holding = true;
            refresh();
        };
        tile->onRelease = [this]
        {
            proc.auditionPattern (pattern, false);
            holding = false;
        };
        addAndMakeVisible (tile);
    }

    keep.onClick = [this]
    {
        const auto index = proc.previewedPatternCandidate();
        if (index < 0)
            return;
        proc.keepPatternCandidate();
        tiles[index + 1]->setKept (true);
        juce::Timer::callAfterDelay (220, [safe = juce::Component::SafePointer<PatternVaryOverlay> (this), index]
        {
            if (safe == nullptr)
                return;
            safe->tiles[index + 1]->setKept (false);
            safe->close();
        });
        refresh();
    };
    back.onClick = [this] { proc.previewPatternCandidate (-1); refresh(); };
    save.onClick = [this]
    {
        if (proc.previewedPatternCandidate() >= 0)
            proc.keepPatternCandidate();
        close();
        if (onSave)
            onSave();
    };
    onClose = [this]
    {
        if (holding)
            proc.auditionPattern (pattern, false);
        holding = false;
        proc.previewPatternCandidate (-1);
    };
    startTimerHz (10);
}

PatternVaryOverlay::~PatternVaryOverlay()
{
    stopTimer();
}

void PatternVaryOverlay::openFor (int p)
{
    pattern = p;
    shownVersion = -1;
    open();
    if (proc.patternVaryPattern() != pattern || proc.patternCandidates().empty())
        startVary();
    refresh();
}

void PatternVaryOverlay::startVary()
{
    static const float amounts[] = { 0.2f, 0.45f, 0.8f };
    std::array<bool, kNumTracks> locked {};
    for (int t = 0; t < kNumTracks; ++t)
        locked[(size_t) t] = trackLocks[t]->getToggleState();
    proc.startPatternVary (pattern, amounts[amount.getSelected()], (PatternDirection) direction.getSelected(), locked);
    refresh();
}

void PatternVaryOverlay::timerCallback()
{
    if (isVisible() && proc.getPatternVaryVersion() != shownVersion)
        refresh();
}

void PatternVaryOverlay::refresh()
{
    const auto changed = proc.getPatternVaryVersion() != shownVersion;
    shownVersion = proc.getPatternVaryVersion();
    const auto& cands = proc.patternCandidates();
    const auto mine = proc.patternVaryPattern() == pattern && ! cands.empty();
    const auto& base = proc.patterns().get().patterns[(size_t) pattern];

    if (changed)
    {
        tiles[0]->set ("ORIGINAL", proc.getPatternName (pattern), &base, base, true);
        for (int i = 0; i < 4; ++i)
        {
            if (mine && i < (int) cands.size())
                tiles[i + 1]->set (juce::String (i + 1).paddedLeft ('0', 2), juce::String::fromUTF8 (cands[(size_t) i].note.c_str()),
                                   &cands[(size_t) i].pattern, base, false);
            else
                tiles[i + 1]->set (juce::String (i + 1).paddedLeft ('0', 2), {}, nullptr, base, false);
        }
    }
    keep.setEnabled (mine && proc.previewedPatternCandidate() >= 0);
    back.setEnabled (mine && proc.previewedPatternCandidate() >= 0);
    status = base.isEmpty() ? juce::String ("EMPTY PATTERN")
           : (proc.patternVaryPattern() == pattern && cands.empty()) ? juce::String ("NO USABLE VARIATIONS") : juce::String();
    repaint();
}

void PatternVaryOverlay::paint (juce::Graphics& g)
{
    Overlay::paint (g);
    drawHeader (g, "VARY", "P" + juce::String (pattern + 1).paddedLeft ('0', 2) + " " + proc.getPatternName (pattern));
    g.saveState();
    g.setOpacity (shown);
    const auto dy = (int) std::round (8.0f * (1.0f - shown));
    drawLabel (g, "Amount", amount.getBounds().translated (-66, dy).withWidth (60));
    drawLabel (g, "Direction", direction.getBounds().translated (-72, dy).withWidth (66));
    drawLabel (g, "Keep as is", trackLocks[0]->getBounds().translated (-84, dy).withWidth (80));
    if (status.isNotEmpty())
    {
        g.setColour (muted);
        g.setFont (mono (9.5f));
        g.drawText (status, save.getBounds().translated (save.getWidth() + 14, dy).withWidth (300), juce::Justification::centredLeft, false);
    }
    g.restoreState();
}

void PatternVaryOverlay::resized()
{
    panel = getLocalBounds().withTrimmedTop (44).withHeight (300);
    auto r = panel.reduced (14, 12);
    closeButton.setBounds (r.getRight() - 64, r.getY() - 2, 64, 20);
    r.removeFromTop (kTagHeight + 12);

    auto row = r.removeFromTop (22);
    row.removeFromLeft (66);
    amount.setBounds (row.removeFromLeft (amount.getIdealWidth()));
    row.removeFromLeft (20 + 72);
    direction.setBounds (row.removeFromLeft (std::min (row.getWidth(), direction.getIdealWidth())));
    r.removeFromTop (10);

    row = r.removeFromTop (26);
    again.setBounds (row.removeFromRight (132));
    row.removeFromLeft (84);
    for (auto* b : trackLocks)
    {
        const auto w = (int) juce::GlyphArrangement::getStringWidth (mono (9.5f, Weight::Bold), "[8]") + 12;
        b->setBounds (row.removeFromLeft (w).withSizeKeepingCentre (w, 20));
        row.removeFromLeft (2);
    }
    r.removeFromTop (12);

    auto tileRow = r.removeFromTop (116);
    const auto w = (tileRow.getWidth() - 4 * 8) / 5;
    for (auto* t : tiles)
    {
        t->setBounds (tileRow.removeFromLeft (w));
        tileRow.removeFromLeft (8);
    }
    r.removeFromTop (12);
    row = r.removeFromTop (24);
    keep.setBounds (row.removeFromLeft (70));
    row.removeFromLeft (6);
    back.setBounds (row.removeFromLeft (56));
    row.removeFromLeft (6);
    save.setBounds (row.removeFromLeft (150));
}
