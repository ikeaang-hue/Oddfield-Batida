#include "KitPage.h"

#include "Engine/Chain/ChainSettings.h"

using namespace batida;

// XyPad -------------------------------------------------------------------------

XyPad::XyPad (juce::AudioProcessorValueTreeState& state)
    : x (state.getParameter (globalParamID (gp::XyX))), y (state.getParameter (globalParamID (gp::XyY)))
{
    setRepaintsOnMouseActivity (false);
    startTimerHz (30);
}

juce::Rectangle<float> XyPad::padArea() const
{
    return getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (34.0f);
}

void XyPad::timerCallback()
{
    // Follow automation and host changes.
    if (x->getValue() != shownX || y->getValue() != shownY)
        repaint();
}

void XyPad::setLock (std::optional<juce::Point<float>> lock)
{
    if (lock != lockPos)
    {
        lockPos = lock;
        repaint();
    }
}

void XyPad::paint (juce::Graphics& g)
{
    const auto r = padArea();
    shownX = x->getValue();
    shownY = y->getValue();

    // Heat shades the background from cool to hot.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a1f1a), r.getX(), r.getY(),
                                             juce::Colour (0xff1c2227), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 6.0f);

    g.setColour (juce::Colours::white.withAlpha (0.07f));
    for (int i = 1; i < 4; ++i)
    {
        g.drawVerticalLine ((int) (r.getX() + r.getWidth() * i / 4.0f), r.getY(), r.getBottom());
        g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * i / 4.0f), r.getX(), r.getRight());
    }

    g.setColour (juce::Colours::lightgrey.withAlpha (0.7f));
    g.setFont (juce::FontOptions (12.0f));
    const auto inner = r.reduced (8.0f).toNearestInt();
    g.drawText ("destroyed", inner, juce::Justification::topLeft);
    g.drawText ("clean", inner, juce::Justification::bottomLeft);

    // Character axis under the pad.
    auto axis = getLocalBounds().removeFromBottom (32);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (juce::Colours::lightgrey);
    g.drawText ("warm", axis.removeFromTop (16), juce::Justification::left);
    g.drawText ("aggressive", getLocalBounds().removeFromBottom (32).removeFromTop (16), juce::Justification::centred);
    g.drawText ("digital", getLocalBounds().removeFromBottom (32).removeFromTop (16), juce::Justification::right);
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (juce::Colours::grey);
    g.drawText ("Character " + juce::String (juce::roundToInt (shownX * 100.0f)) + "%   Heat "
                    + juce::String (juce::roundToInt (shownY * 100.0f)) + "%",
                axis, juce::Justification::centred);

    // The puck
    const juce::Point<float> p (r.getX() + shownX * r.getWidth(), r.getBottom() - shownY * r.getHeight());
    g.setColour (juce::Colours::orange.withAlpha (0.25f));
    g.drawHorizontalLine ((int) p.y, r.getX(), r.getRight());
    g.drawVerticalLine ((int) p.x, r.getY(), r.getBottom());
    g.setColour (juce::Colours::orange);
    g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (p));
    g.setColour (juce::Colours::black);
    g.drawEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (p), 1.5f);

    // Where a sequencer step's lock has moved the chain right now.
    if (lockPos.has_value())
    {
        const juce::Point<float> l (r.getX() + lockPos->x * r.getWidth(), r.getBottom() - lockPos->y * r.getHeight());
        g.setColour (juce::Colours::white);
        g.drawEllipse (juce::Rectangle<float> (24.0f, 24.0f).withCentre (l), 2.0f);
    }
}

void XyPad::setFromMouse (juce::Point<float> p)
{
    const auto r = padArea();
    const auto nx = juce::jlimit (0.0f, 1.0f, (p.x - r.getX()) / r.getWidth());
    const auto ny = juce::jlimit (0.0f, 1.0f, (r.getBottom() - p.y) / r.getHeight());
    x->setValueNotifyingHost (nx);
    y->setValueNotifyingHost (ny);
    repaint();
    if (onMove)
        onMove (nx, ny);
}

void XyPad::mouseDown (const juce::MouseEvent& e)
{
    x->beginChangeGesture();
    y->beginChangeGesture();
    setFromMouse (e.position);
}

void XyPad::mouseDrag (const juce::MouseEvent& e)
{
    setFromMouse (e.position);
}

void XyPad::mouseUp (const juce::MouseEvent&)
{
    x->endChangeGesture();
    y->endChangeGesture();
}

void XyPad::mouseDoubleClick (const juce::MouseEvent&)
{
    for (auto* p : { x, y })
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
    repaint();
}

// KitPage -----------------------------------------------------------------------

namespace
{
std::optional<Pattern> patternClipboard;

void styleLabel (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (12.0f));
    l.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    l.setJustificationType (juce::Justification::centredRight);
}
} // namespace

KitPage::KitPage (BatidaProcessor& p) : VoicePage (p), pad (p.getState()), grid (p)
{
    addAndMakeVisible (pad);
    addAndMakeVisible (grid);

    dynamicsSection = addSection ("Dynamics");
    compAmount = &addGlobal (dynamicsSection, gp::CompAmount, "Amount");
    compAttack = &addGlobal (dynamicsSection, gp::CompAttack, "Attack");
    addGlobal (dynamicsSection, gp::CompRelease, "Release");
    addGlobal (dynamicsSection, gp::CompMix, "Mix");
    addGlobal (dynamicsSection, gp::CompDetector, "Detector").withWidth (96);
    addGlobal (dynamicsSection, gp::CompFollow, "Follow XY");

    distortionSection = addSection ("Distortion");
    drive = &addGlobal (distortionSection, gp::DistDrive, "Drive");
    type = &addGlobal (distortionSection, gp::DistType, "Type");
    lowKeep = &addGlobal (distortionSection, gp::DistLowKeep, "Low Keep");
    exciter = &addGlobal (distortionSection, gp::ExcAmount, "Exciter");
    tone = &addGlobal (distortionSection, gp::ExcTone, "Exc Tone");
    addGlobal (distortionSection, gp::DistFollow, "Follow XY");

    eqSection = addSection ("EQ");
    addGlobal (eqSection, gp::EqHp, "High-pass");
    addGlobal (eqSection, gp::EqLp, "Low-pass");
    addGlobal (eqSection, gp::EqLow, "Low");
    addGlobal (eqSection, gp::EqMid, "Mid");
    eqHigh = &addGlobal (eqSection, gp::EqHigh, "High");
    addGlobal (eqSection, gp::EqFollow, "Follow XY");

    outputSection = addSection ("Output");
    addGlobal (outputSection, gp::ChainOut, "Chain Out");
    addGlobal (outputSection, gp::SafetyClip, "Safety Clip");

    // Transport strip ------------------------------------------------------
    auto& state = p.getState();
    styleLabel (patternLabel, "Pattern");
    styleLabel (lengthLabel, "Length");
    styleLabel (runLabel, "Run");
    styleLabel (quantiseLabel, "Start on");
    styleLabel (tempoLabel, "Tempo");
    styleLabel (swingLabel, "Swing");

    for (int i = 1; i <= kNumPatterns; ++i)
        patternBox.addItem (juce::String (i), i);
    runBox.addItemList ({ "Keys", "Transport" }, 1);
    quantiseBox.addItemList ({ "Step", "Beat", "Bar" }, 1);
    patternAttachment = std::make_unique<ComboAttachment> (state, globalParamID (gp::SeqPattern), patternBox);
    runAttachment = std::make_unique<ComboAttachment> (state, globalParamID (gp::SeqRun), runBox);
    quantiseAttachment = std::make_unique<ComboAttachment> (state, globalParamID (gp::SeqQuantise), quantiseBox);
    latchAttachment = std::make_unique<ButtonAttachment> (state, globalParamID (gp::SeqLatch), latchButton);
    playAttachment = std::make_unique<ButtonAttachment> (state, globalParamID (gp::SeqPlay), playButton);

    for (auto* s : { &tempoSlider, &swingSlider })
        s->setSliderStyle (juce::Slider::LinearBar);
    tempoAttachment = std::make_unique<SliderAttachment> (state, globalParamID (gp::SeqTempo), tempoSlider);
    swingAttachment = std::make_unique<SliderAttachment> (state, globalParamID (gp::SeqSwing), swingSlider);

    lengthSlider.setSliderStyle (juce::Slider::IncDecButtons);
    lengthSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 34, 22);
    lengthSlider.setRange (kMinPatternSteps, kMaxSteps, 1);
    lengthSlider.onValueChange = [this]
    {
        const auto pi = proc.displayPattern();
        const auto len = (int) lengthSlider.getValue();
        if (proc.patterns().get().patterns[(size_t) pi].length == len)
            return;
        proc.patterns().edit ([&] (PatternBank& b)
        {
            auto& pat = b.patterns[(size_t) pi];
            // Tracks that ran the full pattern keep doing so.
            for (auto& t : pat.tracks)
                if (t.length >= pat.length)
                    t.length = len;
            pat.length = len;
        });
    };

    copyButton.onClick = [this] { patternClipboard = proc.patterns().get().patterns[(size_t) proc.displayPattern()]; };
    pasteButton.onClick = [this]
    {
        if (patternClipboard)
            proc.patterns().edit ([&] (PatternBank& b) { b.patterns[(size_t) proc.displayPattern()] = *patternClipboard; });
    };
    clearButton.onClick = [this] { proc.patterns().edit ([&] (PatternBank& b) { b.patterns[(size_t) proc.displayPattern()].clear(); }); };

    static const char* laneNames[] = { "Gate", "Velocity", "Pitch", "Slice", "Ratchet", "Prob" };
    for (int i = 0; i < 6; ++i)
    {
        auto& b = laneButtons[(size_t) i];
        b.setButtonText (laneNames[i]);
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2b7bb9));
        b.onClick = [this, i] { setLane ((PatternGrid::Lane) i, laneButtons[(size_t) i]); };
        addAndMakeVisible (b);
    }
    setLane (PatternGrid::Lane::Gate, laneButtons[0]);

    for (int i = 0; i < 2; ++i)
    {
        auto& b = pageButtons[(size_t) i];
        b.setButtonText (i == 0 ? "Steps 1-32" : "Steps 33-64");
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2b7bb9));
        b.onClick = [this, i]
        {
            grid.setPage (i);
            for (int j = 0; j < 2; ++j)
                pageButtons[(size_t) j].setToggleState (j == i, juce::dontSendNotification);
        };
        addAndMakeVisible (b);
    }
    pageButtons[0].setToggleState (true, juce::dontSendNotification);

    xyRecButton.setTooltip ("While a pattern plays, moving the pad records XY locks into the steps.");
    pad.onMove = [this] (float x, float y)
    {
        const auto& seq = proc.sequencer();
        if (! xyRecButton.getToggleState() || ! seq.isRunning() || seq.getPatternStep() < 0)
            return;
        const auto pi = seq.getActivePattern();
        const auto step = seq.getPatternStep();
        proc.patterns().edit ([&] (PatternBank& b) { b.patterns[(size_t) pi].xy[(size_t) step] = { true, x, y }; });
    };

    hint.setText ("Hold C3-D#4 to play patterns 1-16; C1-G1 play the voices on top.", juce::dontSendNotification);
    hint.setFont (juce::FontOptions (12.0f));
    hint.setColour (juce::Label::textColourId, juce::Colours::grey);

    for (juce::Component* c : { (juce::Component*) &patternLabel, (juce::Component*) &patternBox, (juce::Component*) &lengthLabel,
                                (juce::Component*) &lengthSlider, (juce::Component*) &runLabel, (juce::Component*) &runBox,
                                (juce::Component*) &quantiseLabel, (juce::Component*) &quantiseBox, (juce::Component*) &latchButton,
                                (juce::Component*) &playButton, (juce::Component*) &tempoLabel, (juce::Component*) &tempoSlider,
                                (juce::Component*) &swingLabel, (juce::Component*) &swingSlider, (juce::Component*) &copyButton,
                                (juce::Component*) &pasteButton, (juce::Component*) &clearButton, (juce::Component*) &xyRecButton,
                                (juce::Component*) &hint })
        addAndMakeVisible (c);

    timerCallback(); // markers right away, not only on the first tick
    startTimerHz (20);
}

KitPage::~KitPage()
{
    stopTimer();
}

void KitPage::setLane (PatternGrid::Lane lane, juce::TextButton& button)
{
    grid.setLane (lane);
    for (auto& b : laneButtons)
        b.setToggleState (&b == &button, juce::dontSendNotification);
}

void KitPage::timerCallback()
{
    const auto base = proc.readGlobalParams();
    auto shown = base;
    const auto& kit = proc.getKit();
    if (kit.isXyLocked())
    {
        shown[gp::XyX] = kit.getLockX();
        shown[gp::XyY] = kit.getLockY();
        pad.setLock (juce::Point<float> (kit.getLockX(), kit.getLockY()));
    }
    else
        pad.setLock (std::nullopt);

    const auto e = computeEffectiveChain (shown);

    auto marker = [] (float effective, float knob) -> std::optional<double>
    {
        if (std::abs (effective - knob) <= 1.0e-4f * std::max (1.0f, std::abs (knob)))
            return std::nullopt;
        return (double) effective;
    };

    compAmount->setMarker (marker (e.compAmount, base[gp::CompAmount]));
    compAttack->setMarker (marker (e.compAttack, base[gp::CompAttack]));
    drive->setMarker (marker (e.drive, base[gp::DistDrive]));
    type->setMarker (marker (e.type, base[gp::DistType]));
    lowKeep->setMarker (marker (e.lowKeepHz, base[gp::DistLowKeep]));
    exciter->setMarker (marker (e.excAmount, base[gp::ExcAmount]));
    tone->setMarker (marker (e.excToneHz, base[gp::ExcTone]));
    eqHigh->setMarker (marker (e.highDb, base[gp::EqHigh]));

    const auto gr = proc.getGainReductionDb();
    setSectionTitle (dynamicsSection, gr < -0.1f ? "Dynamics   GR " + juce::String (gr, 1) + " dB" : juce::String ("Dynamics"));
    repaint (0, 0, getWidth(), 30);

    // The strip follows the pattern shown (a pattern key can switch it).
    const auto pi = proc.displayPattern();
    lengthSlider.setValue (proc.patterns().get().patterns[(size_t) pi].length, juce::dontSendNotification);
}

void KitPage::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (362);
    pad.setBounds (top.removeFromLeft (310).withHeight (350));
    top.removeFromLeft (14);
    for (const auto s : { dynamicsSection, distortionSection })
        top.removeFromTop (layoutRow (top, { s }) + 8);
    layoutRow (top, { eqSection, outputSection });

    // Transport strip, two rows
    auto rowA = area.removeFromTop (28);
    auto place = [] (juce::Rectangle<int>& row, juce::Component& c, int w, int gap = 6)
    {
        c.setBounds (row.removeFromLeft (w).reduced (0, 2));
        row.removeFromLeft (gap);
    };
    place (rowA, patternLabel, 50, 2);
    place (rowA, patternBox, 58);
    place (rowA, lengthLabel, 46, 2);
    place (rowA, lengthSlider, 92);
    place (rowA, runLabel, 30, 2);
    place (rowA, runBox, 96);
    place (rowA, quantiseLabel, 56, 2);
    place (rowA, quantiseBox, 70);
    place (rowA, latchButton, 64);
    place (rowA, playButton, 58);
    place (rowA, tempoLabel, 44, 2);
    place (rowA, tempoSlider, 110);
    place (rowA, swingLabel, 42, 2);
    place (rowA, swingSlider, 80);

    area.removeFromTop (4);
    auto rowB = area.removeFromTop (26);
    for (auto& b : laneButtons)
        place (rowB, b, 64, 2);
    rowB.removeFromLeft (10);
    for (auto& b : pageButtons)
        place (rowB, b, 84, 2);
    rowB.removeFromLeft (10);
    place (rowB, xyRecButton, 70);
    place (rowB, copyButton, 54, 2);
    place (rowB, pasteButton, 54, 2);
    place (rowB, clearButton, 54, 10);
    hint.setBounds (rowB);

    area.removeFromTop (6);
    grid.setBounds (area.removeFromTop (PatternGrid::preferredHeight()));
}
