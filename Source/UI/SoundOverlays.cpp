#include "SoundOverlays.h"

#include "Engine/FmSource.h"

using namespace batida;
using namespace theme;

// Overlay --------------------------------------------------------------------------

Overlay::Overlay()
{
    setVisible (false);
    setWantsKeyboardFocus (true);
    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { close(); };
}

void Overlay::open()
{
    setVisible (true);
    toFront (true);
    opening = true;
    start = juce::Time::getMillisecondCounterHiRes();
    animate();
}

void Overlay::close()
{
    if (! isVisible() || ! opening)
        return;
    opening = false;
    start = juce::Time::getMillisecondCounterHiRes();
    animate();
}

void Overlay::animate()
{
    if (vblank == nullptr)
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] (double) { animate(); });

    const auto target = opening ? 1.0f : 0.0f;
    if (juce::exactlyEqual (shown, target))
        return;
    const auto duration = opening ? kBase : kFast;
    const auto k = canAnimate (*this) ? easeOut ((float) ((juce::Time::getMillisecondCounterHiRes() - start) / duration)) : 1.0f;
    shown = opening ? std::max (shown, k) : std::min (shown, 1.0f - k);
    if (k >= 1.0f)
        shown = target;

    // The panel's controls rise 8 px and fade in with it.
    for (auto* c : getChildren())
    {
        c->setAlpha (shown);
        c->setTransform (juce::AffineTransform::translation (0.0f, 8.0f * (1.0f - shown)));
    }
    repaint();
    if (! opening && shown <= 0.0f)
    {
        setVisible (false);
        if (onClose)
            onClose();
    }
}

void Overlay::paint (juce::Graphics& g)
{
    g.fillAll (bg.withAlpha (0.78f * shown));
    const auto p = panel.toFloat().translated (0.0f, 8.0f * (1.0f - shown));
    g.setColour (bg.withAlpha (shown));
    g.fillRect (p);
    g.setColour (ink.withAlpha (shown));
    g.drawRect (p, 1.0f);
}

void Overlay::drawHeader (juce::Graphics& g, const juce::String& tag, const juce::String& title)
{
    g.saveState();
    g.setOpacity (shown);
    const auto dy = (int) std::round (8.0f * (1.0f - shown));
    const auto r = panel.reduced (14, 12).withHeight (kTagHeight).translated (0, dy);
    const auto w = drawTag (g, r, tag, tag == "VARY" ? lime : ink, bg);
    g.setColour (ink);
    g.setFont (head (11.0f, true));
    g.drawText (title.toUpperCase(), r.withTrimmedLeft (w + 10).withWidth (400), juce::Justification::centredLeft, false);
    g.restoreState();
}

void Overlay::mouseDown (const juce::MouseEvent& e)
{
    if (! panel.contains (e.getPosition()))
        close();
}

bool Overlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    return false;
}

// VaryTile -------------------------------------------------------------------------

VaryTile::VaryTile() : vblank (this, [this] (double)
{
    if (held)
        peaks.setPlayhead ((float) std::fmod ((juce::Time::getMillisecondCounterHiRes() - heldSince) / 700.0, 1.0));
})
{
    peaks.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (peaks);
}

void VaryTile::set (const juce::String& n, const juce::String& text, std::vector<float> p, bool isOriginal)
{
    name = n;
    note = text;
    original = isOriginal;
    available = ! p.empty();
    peaks.set (std::move (p), isOriginal ? ink : lime);
    repaint();
}

void VaryTile::setKept (bool k)
{
    kept = k;
    repaint();
}

void VaryTile::resized()
{
    peaks.setBounds (getLocalBounds().reduced (8).withTrimmedTop (18).withHeight (52));
}

void VaryTile::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds();
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
    g.setColour (kept ? bg : muted);
    g.setFont (mono (9.5f));
    g.drawFittedText (note, r.reduced (8, 6).withTop (r.getY() + 80), juce::Justification::topLeft, 2, 1.0f);
}

void VaryTile::mouseDown (const juce::MouseEvent&)
{
    if (! available)
        return;
    held = true;
    heldSince = juce::Time::getMillisecondCounterHiRes();
    if (onPress)
        onPress();
    repaint();
}

void VaryTile::mouseUp (const juce::MouseEvent&)
{
    if (! held)
        return;
    held = false;
    peaks.setPlayhead (-1.0f);
    if (onRelease)
        onRelease();
    repaint();
}

// VaryOverlay ----------------------------------------------------------------------

VaryOverlay::VaryOverlay (BatidaProcessor& p) : proc (p)
{
    for (juce::Component* c : { (juce::Component*) &scope, (juce::Component*) &amount, (juce::Component*) &direction, (juce::Component*) &lockSource,
                                (juce::Component*) &lockFx, (juce::Component*) &lockEnvelopes, (juce::Component*) &again,
                                (juce::Component*) &keep, (juce::Component*) &back, (juce::Component*) &save })
        addAndMakeVisible (c);
    again.withDice = true;
    again.onClick = [this] { startVary(); };

    for (int v = 0; v < kNumVoices; ++v)
    {
        auto* b = slotLocks.add (new juce::ToggleButton (juce::String (v + 1)));
        addChildComponent (b);
    }
    scope.onChange = [this] (int)
    {
        proc.previewCandidate (-1);
        proc.previewKitCandidate (-1);
        for (auto* b : slotLocks)
            b->setVisible (isKitScope());
        save.setButtonText (juce::String::fromUTF8 (isKitScope() ? "SAVE KIT\xe2\x80\xa6" : "SAVE TO LIBRARY\xe2\x80\xa6"));
        shownVersion = shownResults = -1;
        refresh();
        startIfNeeded();
    };

    for (int i = 0; i <= 4; ++i)
    {
        auto* t = tiles.add (new VaryTile());
        t->onPress = [this, i]
        {
            if (isKitScope())
            {
                proc.previewKitCandidate (i - 1);
                startHold();
            }
            else if (i == 0)
            {
                proc.previewCandidate (-1);
                proc.audition (voice, true);
            }
            else
            {
                proc.previewCandidate (i - 1);
                proc.audition (proc.varyVoice(), true);
            }
            refresh();
        };
        t->onRelease = [this, i]
        {
            if (isKitScope())
                endHold();
            else
                proc.audition (i == 0 ? voice : proc.varyVoice(), false);
        };
        addAndMakeVisible (t);
    }

    keep.onClick = [this]
    {
        if (isKitScope())
        {
            const auto index = proc.previewedKitCandidate();
            if (index < 0)
                return;
            proc.keepKitCandidate();
            tiles[index + 1]->setKept (true);
            juce::Timer::callAfterDelay (220, [safe = juce::Component::SafePointer<VaryOverlay> (this), index]
            {
                if (safe == nullptr)
                    return;
                safe->tiles[index + 1]->setKept (false);
                safe->close();
            });
            refresh();
            return;
        }
        const auto index = proc.previewedCandidate();
        if (index < 0)
            return;
        proc.keepCandidate();
        tiles[index + 1]->setKept (true);
        juce::Timer::callAfterDelay (220, [safe = juce::Component::SafePointer<VaryOverlay> (this), index]
        {
            if (safe == nullptr)
                return;
            safe->tiles[index + 1]->setKept (false);
            safe->close();
        });
        refresh();
    };
    back.onClick = [this]
    {
        proc.previewCandidate (-1);
        proc.previewKitCandidate (-1);
        refresh();
    };
    save.onClick = [this]
    {
        const auto kitScope = isKitScope();
        if (kitScope && proc.previewedKitCandidate() >= 0)
            proc.keepKitCandidate();
        else if (! kitScope && proc.previewedCandidate() >= 0)
            proc.keepCandidate();
        close();
        if (kitScope ? onSaveKit : onSave)
            (kitScope ? onSaveKit : onSave)();
    };
    // Closing without KEEP goes back to the sounds as they were.
    onClose = [this]
    {
        if (heldPattern >= 0)
            endHold();
        proc.previewCandidate (-1);
        proc.previewKitCandidate (-1);
    };
    startTimerHz (10);
}

// Holding a kit tile plays the pattern on screen (or, with an empty
// pattern, the selected sound) with the suggestion in place.
void VaryOverlay::startHold()
{
    const auto pattern = proc.displayPattern();
    if (! proc.patterns().get().patterns[(size_t) pattern].isEmpty())
    {
        heldPattern = pattern;
        proc.auditionPattern (pattern, true);
    }
    else
    {
        heldPattern = -1;
        proc.audition (voice, true);
    }
}

void VaryOverlay::endHold()
{
    if (heldPattern >= 0)
        proc.auditionPattern (heldPattern, false);
    else
        proc.audition (voice, false);
    heldPattern = -1;
}

void VaryOverlay::startIfNeeded()
{
    if (isKitScope() ? ! proc.hasKitVary() : (proc.varyVoice() != voice || proc.varyCandidates().empty()))
        startVary();
    else
        refresh();
}

std::vector<float> VaryOverlay::kitPeaks (const std::array<VoiceParams, kNumVoices>& params) const
{
    // The eight sounds side by side, a gap between each.
    std::vector<float> all;
    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto p = renderPeaks (params[(size_t) v], proc.sampleSlot (v).getDisplayData(), 11, 0.5);
        all.insert (all.end(), p.begin(), p.end());
        all.push_back (0.0f);
    }
    return all;
}

VaryOverlay::~VaryOverlay()
{
    stopTimer();
}

void VaryOverlay::setVoice (int v)
{
    if (v != voice && proc.previewedCandidate() >= 0 && ! isKitScope())
        proc.previewCandidate (-1);
    voice = v;
    shownVersion = shownResults = -1;
    if (isVisible()) // hidden, it renders nothing; the timer catches up once it opens
        refresh();
}

void VaryOverlay::startVary()
{
    static const float amounts[] = { 0.18f, 0.4f, 0.75f };
    if (isKitScope())
    {
        std::array<bool, kNumVoices> locked {};
        for (int v = 0; v < kNumVoices; ++v)
            locked[(size_t) v] = slotLocks[v]->getToggleState();
        proc.startKitVary (amounts[amount.getSelected()], (VaryDirection) direction.getSelected(), lockSource.getToggleState(),
                           lockFx.getToggleState(), lockEnvelopes.getToggleState(), locked);
    }
    else
        proc.startVary (voice, amounts[amount.getSelected()], (VaryDirection) direction.getSelected(), lockSource.getToggleState(),
                        lockFx.getToggleState(), lockEnvelopes.getToggleState());
    refresh();
}

void VaryOverlay::timerCallback()
{
    if (isVisible() && (proc.getVaryVersion() != shownVersion || proc.getVaryResultsVersion() != shownResults))
        refresh();
}

void VaryOverlay::refresh()
{
    // Tiles are rendered only when the suggestions change, not when one is
    // pressed (a press only changes which one is heard).
    const auto versionChanged = proc.getVaryResultsVersion() != shownResults;
    shownResults = proc.getVaryResultsVersion();
    shownVersion = proc.getVaryVersion();

    if (isKitScope())
    {
        const auto& kits = proc.kitCandidates();
        const auto has = proc.hasKitVary() && ! kits.empty();
        if (versionChanged)
        {
            std::array<VoiceParams, kNumVoices> base;
            for (int v = 0; v < kNumVoices; ++v)
                base[(size_t) v] = has ? proc.kitVaryBase()[(size_t) v] : proc.readVoiceParams (v);
            tiles[0]->set ("ORIGINAL", proc.getOrigins().kitName, kitPeaks (base), true);
            for (int i = 0; i < 4; ++i)
            {
                if (has && i < (int) kits.size())
                    tiles[i + 1]->set (juce::String (i + 1).paddedLeft ('0', 2), juce::String::fromUTF8 (kits[(size_t) i].note.c_str()),
                                       kitPeaks (kits[(size_t) i].params), false);
                else
                    tiles[i + 1]->set (juce::String (i + 1).paddedLeft ('0', 2), {}, {}, false);
            }
        }
        keep.setEnabled (has && proc.previewedKitCandidate() >= 0);
        back.setEnabled (has && proc.previewedKitCandidate() >= 0);
        again.setEnabled (! proc.isKitVarying());
        status = proc.isKitVarying() ? juce::String::fromUTF8 ("VARYING\xe2\x80\xa6")
               : (proc.hasKitVary() && kits.empty()) ? juce::String ("NO USABLE VARIATIONS") : juce::String();
        repaint();
        return;
    }
    const auto& cands = proc.varyCandidates();
    const bool mine = proc.varyVoice() == voice && ! cands.empty();
    const auto* sample = proc.sampleSlot (voice).getDisplayData();

    if (versionChanged)
    {
        const auto base = mine ? proc.varyBase() : proc.readVoiceParams (voice);
        tiles[0]->set ("ORIGINAL", proc.getVoiceName (voice), renderPeaks (base, sample, 96), true);
        for (int i = 0; i < 4; ++i)
        {
            if (mine && i < (int) cands.size())
                tiles[i + 1]->set (juce::String (i + 1).paddedLeft ('0', 2), juce::String::fromUTF8 (cands[(size_t) i].note.c_str()),
                                   renderPeaks (cands[(size_t) i].params, sample, 96), false);
            else
                tiles[i + 1]->set (juce::String (i + 1).paddedLeft ('0', 2), {}, {}, false);
        }
    }

    keep.setEnabled (mine && proc.previewedCandidate() >= 0);
    back.setEnabled (mine && proc.previewedCandidate() >= 0);
    again.setEnabled (! proc.isVarying());

    if (proc.isVarying())
        status = juce::String::fromUTF8 ("VARYING\xe2\x80\xa6");
    else if (proc.varyVoice() == voice && cands.empty())
        status = "NO USABLE VARIATIONS";
    else
        status = {};
    repaint();
}

void VaryOverlay::paint (juce::Graphics& g)
{
    Overlay::paint (g);
    drawHeader (g, "VARY", isKitScope() ? "Kit " + proc.getOrigins().kitName
                                        : juce::String (voice + 1).paddedLeft ('0', 2) + " " + proc.getVoiceName (voice));
    g.saveState();
    g.setOpacity (shown);
    const auto dy = std::round (8.0f * (1.0f - shown));
    drawLabel (g, "Amount", amount.getBounds().translated (-66, (int) dy).withWidth (60));
    drawLabel (g, "Direction", direction.getBounds().translated (-72, (int) dy).withWidth (66));
    drawLabel (g, "Keep as is", lockSource.getBounds().translated (-84, (int) dy).withWidth (80));
    if (status.isNotEmpty())
    {
        g.setColour (muted);
        g.setFont (mono (9.5f));
        g.drawText (status, save.getBounds().translated (save.getWidth() + 14, (int) dy).withWidth (300), juce::Justification::centredLeft, false);
    }
    g.restoreState();
}

void VaryOverlay::resized()
{
    panel = getLocalBounds().withTrimmedTop (44).withHeight (300);
    auto r = panel.reduced (14, 12);
    closeButton.setBounds (r.getRight() - 64, r.getY() - 2, 64, 20);
    scope.setBounds (closeButton.getX() - 16 - scope.getIdealWidth(), r.getY(), scope.getIdealWidth(), kTagHeight);
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
    for (auto* b : { &lockSource, &lockFx, &lockEnvelopes })
    {
        const auto w = (int) juce::GlyphArrangement::getStringWidth (mono (9.5f, Weight::Bold), "[" + b->getButtonText().toUpperCase() + "]") + 14;
        b->setBounds (row.removeFromLeft (w).withSizeKeepingCentre (w, 20));
        row.removeFromLeft (6);
    }
    row.removeFromLeft (14);
    for (auto* b : slotLocks) // KIT: slots kept as they are
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

// OperatorEditor -------------------------------------------------------------------

namespace
{
const char* kOpLabels[] = { "Ratio", "Fixed", "Freq", "Level", "Wave", "Fold", "Attack", "Decay", "Sustain", "Release" };
} // namespace

OperatorEditor::OperatorEditor (BatidaProcessor& p) : proc (p)
{
    auto add = [this] (int param, const juce::String& label) -> ParamControl&
    {
        auto* c = controls.add (new ParamControl (proc.getState(), label));
        bound.emplace_back (c, param);
        c->bind (voiceParamID (0, param));
        addAndMakeVisible (c);
        return *c;
    };
    algorithm = &add (vp::FmAlgo, "Algorithm");
    algorithm->withLabelWidth (64);
    pitch = &add (vp::FmPitch, "Pitch");
    pitch->withLabelWidth (40);
    feedback = &add (vp::FmFeedback, "Feedback");
    feedback->withLabelWidth (60);
    for (int o = 0; o < kNumOps; ++o)
    {
        for (int f = 0; f < kNumOpFields; ++f)
        {
            auto& c = add (opParam (o, f), kOpLabels[f]);
            c.withLabelWidth (f == Freq ? 30 : 50).withValueWidth (58);
            opControls[(size_t) o][(size_t) f] = &c;
        }
        addAndMakeVisible (glyphs.add (new WaveGlyph()));
        addAndMakeVisible (envs.add (new MiniEnvelope()));
    }
    macros = { &add (vp::FmHarm, "Harmonic"), &add (vp::FmBright, "Bright"), &add (vp::FmBrightDecay, "Br. decay"), &add (vp::FmGrit, "Grit") };
    startTimerHz (30);
}

OperatorEditor::~OperatorEditor()
{
    stopTimer();
}

void OperatorEditor::bindVoice (int v)
{
    voice = v;
    for (auto& [c, param] : bound)
        c->bind (voiceParamID (v, param));
    timerCallback();
}

void OperatorEditor::timerCallback()
{
    if (! isVisible())
        return;
    const auto p = proc.readVoiceParams (voice);
    const auto fm = computeEffectiveFm (p);
    auto marker = [] (float effective, float base) -> std::optional<double>
    {
        if (std::abs (effective - base) <= 1.0e-4f * std::max (1.0f, std::abs (base)))
            return std::nullopt;
        return (double) effective;
    };
    feedback->setMarker (marker (fm.feedback, p[vp::FmFeedback]));
    for (int o = 0; o < kNumOps; ++o)
    {
        const auto& e = fm.ops[(size_t) o];
        auto& c = opControls[(size_t) o];
        c[Ratio]->setMarker (marker (e.ratio, p.op (o, Ratio)));
        c[OpLevel]->setMarker (marker (e.level, p.op (o, OpLevel)));
        c[OpD]->setMarker (marker (e.decay, p.op (o, OpD)));
        c[OpR]->setMarker (marker (e.release, p.op (o, OpR)));
        c[Ratio]->setAlpha (e.fixed ? 0.4f : 1.0f);
        c[Freq]->setAlpha (e.fixed ? 1.0f : 0.4f);
        glyphs[o]->set (p.op (o, Wave));
        envs[o]->set (e.attack, e.decay, e.sustain, e.release);
        if (carriers[(size_t) o] != e.carrier || active[(size_t) o] != (e.level > 0.0f))
        {
            carriers[(size_t) o] = e.carrier;
            active[(size_t) o] = e.level > 0.0f;
            repaint();
        }
    }
}

void OperatorEditor::paint (juce::Graphics& g)
{
    Overlay::paint (g);
    drawHeader (g, "OPERATORS", juce::String (voice + 1).paddedLeft ('0', 2) + " " + proc.getVoiceName (voice));
    g.saveState();
    g.setOpacity (shown);
    const auto dy = std::round (8.0f * (1.0f - shown));
    for (int o = 0; o < kNumOps; ++o)
    {
        const auto c = columns[(size_t) o].translated (0, (int) dy);
        g.setColour (line);
        g.drawRect (c, 1);
        g.setColour (active[(size_t) o] ? ink : muted);
        g.setFont (head (11.5f, true));
        g.drawText ("OP" + juce::String (o + 1), c.reduced (8, 6).withHeight (16), juce::Justification::centredLeft, false);
        drawLabel (g, carriers[(size_t) o] ? "carrier" : "mod", c.reduced (8, 6).withHeight (16), muted, juce::Justification::centredRight);
    }
    const auto m = macros[0]->getBounds().translated (0, (int) dy);
    g.setColour (ink);
    g.setFont (head (8.5f));
    g.drawText ("MACROS", m.withY (m.getY() - 18).withHeight (14), juce::Justification::centredLeft, false);
    g.restoreState();
}

void OperatorEditor::resized()
{
    panel = getLocalBounds().withHeight (std::min (getHeight(), 470));
    auto r = panel.reduced (14, 12);
    closeButton.setBounds (r.getRight() - 64, r.getY() - 2, 64, 20);
    r.removeFromTop (kTagHeight + 12);

    auto top = r.removeFromTop (22);
    algorithm->setBounds (top.removeFromLeft (230));
    top.removeFromLeft (24);
    pitch->setBounds (top.removeFromLeft (250));
    top.removeFromLeft (24);
    feedback->setBounds (top.removeFromLeft (230));
    r.removeFromTop (10);

    auto cols = r.removeFromTop (300);
    const auto w = (cols.getWidth() - 3 * 8) / 4;
    for (int o = 0; o < kNumOps; ++o)
    {
        auto c = cols.removeFromLeft (w);
        cols.removeFromLeft (8);
        columns[(size_t) o] = c;
        auto inner = c.reduced (8, 6);
        const auto head = inner.removeFromTop (16);
        glyphs[o]->setBounds (head.withSizeKeepingCentre (22, 12).withX (head.getX() + 40));
        inner.removeFromTop (6);
        envs[o]->setBounds (inner.removeFromTop (32));
        inner.removeFromTop (8);
        auto& oc = opControls[(size_t) o];
        RowLayout rows { inner };
        oc[Ratio]->setBounds (rows.next());
        auto fixedRow = rows.next();
        oc[Fixed]->setBounds (fixedRow.removeFromLeft (oc[Fixed]->getIdealWidth()));
        fixedRow.removeFromLeft (8);
        oc[Freq]->setBounds (fixedRow);
        for (const auto f : { OpLevel, Wave, Fold, OpA, OpD, OpS, OpR })
            oc[(size_t) f]->setBounds (rows.next());
    }
    r.removeFromTop (28);
    auto macroRow = r.removeFromTop (20);
    const auto mw = (macroRow.getWidth() - 3 * 16) / 4;
    for (auto* m : macros)
    {
        m->setBounds (macroRow.removeFromLeft (mw));
        macroRow.removeFromLeft (16);
    }
}
