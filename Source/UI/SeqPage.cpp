#include "SeqPage.h"

using namespace batida;
using namespace theme;

namespace
{
std::optional<Pattern> patternClipboard;

juce::RangedAudioParameter& paramOf (BatidaProcessor& p, int g)
{
    return *p.getState().getParameter (globalParamID (g));
}
} // namespace

// MidiHandle -----------------------------------------------------------------------

MidiHandle::MidiHandle()
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("Pattern as MIDI");
}

void MidiHandle::paint (juce::Graphics& g)
{
    const auto over = isMouseOver() || dragging;
    g.setColour (over ? ink : line);
    g.drawRect (getLocalBounds(), 1);
    g.setColour (over ? ink : muted);
    g.setFont (mono (10.0f, Weight::Bold));
    g.drawText (juce::String::fromUTF8 ("MIDI \xe2\x86\x97"), getLocalBounds(), juce::Justification::centred, false);
}

void MidiHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 5 || ! onExport)
        return;
    dragging = true;
    repaint();
    const auto file = onExport();
    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                    [safe = juce::Component::SafePointer<MidiHandle> (this)]
                                                                    {
                                                                        if (safe != nullptr)
                                                                        {
                                                                            safe->dragging = false;
                                                                            safe->repaint();
                                                                        }
                                                                    });
    // A failed export keeps `dragging` set until the mouse goes up, so it's
    // reported once, not on every move.
}

void MidiHandle::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        dragging = false; // a failed drag (a finished one resets in its callback too)
        repaint();
        return;
    }
    if (onExport)
        if (const auto file = onExport(); file.existsAsFile())
            file.revealToUser();
}

// PatternSlots ---------------------------------------------------------------------

PatternSlots::PatternSlots (BatidaProcessor& p) : proc (p)
{
    startTimerHz (10);
}

juce::Rectangle<int> PatternSlots::slot (int i) const
{
    const auto w = (getWidth() - 15 * 2) / 16;
    return { i * (w + 2), (getHeight() - w) / 2, w, w };
}

void PatternSlots::paint (juce::Graphics& g)
{
    const auto shown = proc.displayPattern();
    const auto& seq = proc.sequencer();
    const auto playing = seq.isRunning() ? seq.getActivePattern() : -1;
    const auto& bank = proc.patterns().get();
    for (int i = 0; i < kNumPatterns; ++i)
    {
        const auto r = slot (i);
        const auto on = i == shown;
        const auto used = ! bank.patterns[(size_t) i].isEmpty();
        if (on)
        {
            g.setColour (ink);
            g.fillRect (r);
        }
        g.setColour (on ? ink : used ? faint : line);
        g.drawRect (r, 1);
        g.setColour (on ? bg : used ? ink : muted);
        g.setFont (mono (8.5f, used || on ? Weight::Bold : Weight::Regular));
        g.drawText (juce::String (i + 1), r, juce::Justification::centred, false);
        if (i == playing)
        {
            g.setColour (lime);
            g.fillRect (r.getRight() - 5, r.getY() + 1, 4, 4);
        }
    }
}

void PatternSlots::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < kNumPatterns; ++i)
        if (slot (i).expanded (1).contains (e.getPosition()))
        {
            proc.setPatternParameter (i);
            repaint();
            return;
        }
}

// SeqPage --------------------------------------------------------------------------

SeqPage::SeqPage (BatidaProcessor& p)
    : proc (p), grid (p), pageMap (p, grid), slots (p),
      tempo (paramOf (p, gp::SeqTempo), 2, "bpm"),
      sync (p.getState(), "Sync"), play (p.getState(), juce::String::fromUTF8 ("\xe2\x96\xb6 Play")), run (p.getState(), "Run"),
      quantise (p.getState(), "Start on"), latch (p.getState(), "Latch"), swing (p.getState(), "Swing"), vary (p)
{
    sync.bind (globalParamID (gp::SeqSync));
    play.bind (globalParamID (gp::SeqPlay));
    run.withLabelWidth (26).bind (globalParamID (gp::SeqRun));
    quantise.withLabelWidth (58).bind (globalParamID (gp::SeqQuantise));
    latch.bind (globalParamID (gp::SeqLatch));
    swing.withLabelWidth (40).withValueWidth (38).bind (globalParamID (gp::SeqSwing));
    sync.setTooltip ("Follow the host");

    // While following the project, show its tempo instead of our own.
    tempo.overrideText = [this]() -> std::optional<juce::String>
    {
        const auto& kit = proc.getKit();
        if (kit.isFollowingHost())
            return juce::String (kit.getHostBpm(), 2) + " bpm";
        return std::nullopt;
    };
    tempo.setComponentID ("tempo");

    shorter.onClick = [this] { setLength (proc.patterns().get().patterns[(size_t) proc.displayPattern()].length - 1); };
    longer.onClick = [this] { setLength (proc.patterns().get().patterns[(size_t) proc.displayPattern()].length + 1); };
    shorter.setRepeatSpeed (400, 60);
    longer.setRepeatSpeed (400, 60);

    copyButton.onClick = [this] { patternClipboard = proc.patterns().get().patterns[(size_t) proc.displayPattern()]; };
    pasteButton.onClick = [this]
    {
        if (patternClipboard)
        {
            proc.beginUndoStep();
            proc.patterns().edit ([&] (PatternBank& b) { b.patterns[(size_t) proc.displayPattern()] = *patternClipboard; });
        }
    };
    clearButton.onClick = [this]
    {
        proc.beginUndoStep();
        proc.patterns().edit ([&] (PatternBank& b) { b.patterns[(size_t) proc.displayPattern()].clear(); });
    };

    varyButton.onClick = [this] { vary.openFor (proc.displayPattern()); };
    vary.onSave = [this] { if (onSavePattern) onSavePattern(); };
    midi.onExport = [this]
    {
        juce::String error;
        const auto file = proc.exportPatternMidi (proc.displayPattern(), &error);
        if (! file.existsAsFile())
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Couldn't export").withMessage (error).withButton ("OK"), nullptr);
        return file;
    };

    lanes.onChange = [this] (int i) { grid.setLane ((PatternGrid::Lane) i); };
    grid.onPageChanged = [this] { pageMap.repaint(); repaint (rulerBounds); };
    grid.setComponentID ("grid");

    for (int t = 0; t < kNumTracks; ++t)
        addAndMakeVisible (tracks.add (new SoundCell (t)));

    for (juce::Component* c : { (juce::Component*) &patternStrip, (juce::Component*) &slots, (juce::Component*) &tempo,
                                (juce::Component*) &sync, (juce::Component*) &play, (juce::Component*) &run, (juce::Component*) &quantise,
                                (juce::Component*) &latch, (juce::Component*) &swing, (juce::Component*) &shorter, (juce::Component*) &longer,
                                (juce::Component*) &copyButton, (juce::Component*) &pasteButton, (juce::Component*) &clearButton,
                                (juce::Component*) &varyButton, (juce::Component*) &midi,
                                (juce::Component*) &lanes, (juce::Component*) &pageMap, (juce::Component*) &grid })
        addAndMakeVisible (c);
    addChildComponent (vary);

    timerCallback();
    startTimerHz (15);
}

SeqPage::~SeqPage()
{
    stopTimer();
}

std::array<SoundCell*, kNumTracks> SeqPage::getTrackCells()
{
    std::array<SoundCell*, kNumTracks> a {};
    for (int t = 0; t < kNumTracks; ++t)
        a[(size_t) t] = tracks[t];
    return a;
}

void SeqPage::setBreakDrop (bool on)
{
    if (breakDrop != on)
    {
        breakDrop = on;
        repaint();
    }
}

void SeqPage::paintOverChildren (juce::Graphics& g)
{
    if (! breakDrop)
        return;
    const auto r = grid.getBounds();
    g.setColour (bg.withAlpha (0.6f));
    g.fillRect (r);
    g.setColour (lime);
    g.drawRect (r, 2);
    const auto label = juce::String::fromUTF8 ("BREAK \xe2\x86\x92 KIT \xc2\xb7 P") + juce::String (proc.displayPattern() + 1).paddedLeft ('0', 2);
    g.setFont (head (12.0f, true));
    const auto w = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 28;
    const auto box = r.withSizeKeepingCentre (w, 32);
    g.setColour (lime);
    g.fillRect (box);
    g.setColour (bg);
    g.drawText (label, box, juce::Justification::centred, false);
}

void SeqPage::setLength (int len)
{
    const auto pi = proc.displayPattern();
    len = juce::jlimit (kMinPatternSteps, kMaxSteps, len);
    if (proc.patterns().get().patterns[(size_t) pi].length == len)
        return;
    proc.beginUndoStep();
    proc.patterns().edit ([&] (PatternBank& b)
    {
        auto& pat = b.patterns[(size_t) pi];
        for (auto& t : pat.tracks) // tracks that ran the full pattern keep doing so
            if (t.length >= pat.length)
                t.length = len;
        pat.length = len;
    });
}

void SeqPage::timerCallback()
{
    const auto& pat = proc.patterns().get().patterns[(size_t) proc.displayPattern()];
    if (pat.length != shownLength)
    {
        shownLength = pat.length;
        repaint (lengthBox);
        repaint (pageText);
    }
    const auto& seq = proc.sequencer();
    const auto step = seq.isRunning() ? seq.getPatternStep() : -1;
    if (step != shownStep || grid.getPage() != shownPage)
    {
        shownStep = step;
        shownPage = grid.getPage();
        repaint (rulerBounds);
        repaint (pageText);
    }
    patternStrip.setSubtitle (proc.getPatternName (proc.displayPattern()));
}

void SeqPage::paint (juce::Graphics& g)
{
    // Length
    g.setColour (line);
    g.drawRect (lengthBox, 1);
    g.setColour (ink);
    g.setFont (mono (11.0f, Weight::Bold));
    g.drawText (juce::String (shownLength), lengthBox, juce::Justification::centred);
    drawLabel (g, "Length", lengthLabel);
    for (auto* t : tracks)
    {
        g.setColour (line);
        g.drawRect (t->getBounds().expanded (1), 1);
    }
    drawLabel (g, "Tempo", tempo.getBounds().translated (-48, 0).withWidth (44));
    drawLabel (g, "Edit", lanes.getBounds().translated (-34, 0).withWidth (30));
    drawLabel (g, "Bar", pageMap.getBounds().translated (-30, 0).withWidth (26));

    // Which steps are on screen.
    const auto first = grid.getPage() * PatternGrid::kStepsPerPage;
    g.setColour (muted);
    g.setFont (mono (9.5f));
    g.drawText (juce::String (first + 1) + juce::String::fromUTF8 ("\xe2\x80\x93") + juce::String (first + PatternGrid::kStepsPerPage) + " of "
                    + juce::String (shownLength),
                pageText, juce::Justification::centredLeft, false);

    // Step ruler over the grid: beats bright, the playhead lime.
    const auto cellW = (float) grid.getWidth() / PatternGrid::kStepsPerPage;
    for (int i = 0; i < PatternGrid::kStepsPerPage; ++i)
    {
        const auto step = first + i;
        const juce::Rectangle<float> r ((float) grid.getX() + (float) i * cellW, (float) rulerBounds.getY(), cellW, (float) rulerBounds.getHeight());
        g.setColour (step == shownStep ? lime : (step % 4 == 0 ? ink : faint));
        g.setFont (mono (9.0f, step % 4 == 0 ? Weight::Medium : Weight::Regular));
        g.drawText (juce::String (step + 1), r, juce::Justification::centredLeft, false);
    }

    // The XY lane's header.
    g.setColour (line);
    g.drawRect (xyHeaderBounds, 1);
    g.setColour (lime);
    g.setFont (mono (10.5f, Weight::Bold));
    g.drawText ("XY LOCK", xyHeaderBounds.reduced (8, 0).withTrimmedBottom (xyHeaderBounds.getHeight() / 2).withTrimmedTop (4),
                juce::Justification::bottomLeft, false);
}

void SeqPage::resized()
{
    auto r = getLocalBounds();

    auto row1 = r.removeFromTop (34);
    patternStrip.setBounds (row1.removeFromLeft (170).withTrimmedTop (2));
    row1.removeFromLeft (14);
    slots.setBounds (row1.removeFromLeft (16 * 21 - 2).withSizeKeepingCentre (16 * 21 - 2, 21));
    auto right = row1;
    play.setBounds (right.removeFromRight (play.getIdealWidth()).withSizeKeepingCentre (play.getIdealWidth(), 22));
    right.removeFromRight (8);
    sync.setBounds (right.removeFromRight (sync.getIdealWidth()).withSizeKeepingCentre (sync.getIdealWidth(), 22));
    right.removeFromRight (8);
    tempo.setBounds (right.removeFromRight (124).withSizeKeepingCentre (124, 26));
    r.removeFromTop (8);

    auto row2 = r.removeFromTop (22);
    lengthLabel = row2.removeFromLeft (58);
    shorter.setBounds (row2.removeFromLeft (22));
    row2.removeFromLeft (2);
    lengthBox = row2.removeFromLeft (36);
    row2.removeFromLeft (2);
    longer.setBounds (row2.removeFromLeft (22));
    row2.removeFromLeft (26);
    run.setBounds (row2.removeFromLeft (run.getIdealWidth()));
    row2.removeFromLeft (26);
    quantise.setBounds (row2.removeFromLeft (quantise.getIdealWidth()));
    row2.removeFromLeft (26);
    latch.setBounds (row2.removeFromLeft (latch.getIdealWidth()));
    swing.setBounds (row2.removeFromRight (220));
    r.removeFromTop (8);

    auto row3 = r.removeFromTop (22);
    row3.removeFromLeft (34);
    lanes.setBounds (row3.removeFromLeft (lanes.getIdealWidth()));
    auto buttons = row3.removeFromRight (5 * 58 + 16 + 10);
    varyButton.setBounds (buttons.removeFromLeft (58));
    buttons.removeFromLeft (4);
    midi.setBounds (buttons.removeFromLeft (58));
    buttons.removeFromLeft (14);
    copyButton.setBounds (buttons.removeFromLeft (58));
    buttons.removeFromLeft (4);
    pasteButton.setBounds (buttons.removeFromLeft (58));
    buttons.removeFromLeft (4);
    clearButton.setBounds (buttons);
    row3.removeFromLeft (40);
    pageMap.setBounds (row3.removeFromLeft (176).withSizeKeepingCentre (176, 16));
    row3.removeFromLeft (10);
    pageText = row3.removeFromLeft (100);
    r.removeFromTop (8);

    rulerBounds = r.removeFromTop (12);
    r.removeFromTop (3);
    auto headers = r.removeFromLeft (148);
    r.removeFromLeft (6);
    grid.setBounds (r);
    rulerBounds = rulerBounds.withLeft (r.getX());
    const auto rh = grid.rowHeight();
    for (int t = 0; t < kNumTracks; ++t)
        tracks[t]->setBounds (headers.getX() + 1, grid.getY() + t * rh + 1, headers.getWidth() - 2, rh - 5);
    xyHeaderBounds = { headers.getX(), grid.getY() + kNumTracks * rh, headers.getWidth(), rh - 3 };
    vary.setBounds (getLocalBounds());
}
