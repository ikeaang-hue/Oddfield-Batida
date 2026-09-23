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

KitPage::KitPage (BatidaProcessor& p) : VoicePage (p), pad (p.getState())
{
    addAndMakeVisible (pad);

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

    chainSection = addSection ("Chain amount per voice (0% = dry)");
    for (int v = 0; v < kNumVoices; ++v)
        addFixedVoice (chainSection, v, vp::ChainAmt,
                       juce::String (v + 1) + " " + juce::MidiMessage::getMidiNoteName (kDrumMapFirstNote + v, true, true, 3));

    xyRecButton.setTooltip ("While a pattern plays, moving the pad records XY locks into its steps (see SEQ).");
    addAndMakeVisible (xyRecButton);
    pad.onMove = [this] (float x, float y)
    {
        const auto& seq = proc.sequencer();
        if (! xyRecButton.getToggleState() || ! seq.isRunning() || seq.getPatternStep() < 0)
            return;
        const auto pi = seq.getActivePattern();
        const auto step = seq.getPatternStep();
        proc.patterns().edit ([&] (PatternBank& b) { b.patterns[(size_t) pi].xy[(size_t) step] = { true, x, y }; });
    };

    timerCallback(); // markers right away, not only on the first tick
    startTimerHz (20);
}

KitPage::~KitPage()
{
    stopTimer();
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
}

void KitPage::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto left = area.removeFromLeft (310);
    pad.setBounds (left.removeFromTop (340));
    left.removeFromTop (6);
    xyRecButton.setBounds (left.removeFromTop (24).withWidth (90));
    left.removeFromTop (8);
    layoutRow (left, { outputSection });

    area.removeFromLeft (14);
    for (const auto s : { dynamicsSection, distortionSection, eqSection, chainSection })
        area.removeFromTop (layoutRow (area, { s }) + 8);
}
