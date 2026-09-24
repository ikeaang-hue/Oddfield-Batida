#include "KitPage.h"

#include "Engine/Chain/ChainSettings.h"

using namespace batida;
using namespace theme;

// XyPad ----------------------------------------------------------------------------

XyPad::XyPad (BatidaProcessor& p)
    : proc (p), x (p.getState().getParameter (globalParamID (gp::XyX))), y (p.getState().getParameter (globalParamID (gp::XyY))),
      vblank (this, [this] (double) { frame(); })
{
    setRepaintsOnMouseActivity (false);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

juce::Point<float> XyPad::getPosition() const
{
    return { x->getValue(), y->getValue() };
}

juce::Point<float> XyPad::targetEffective() const
{
    const auto& kit = proc.getKit();
    if (kit.isXyLocked())
        return { kit.getLockX(), kit.getLockY() };

    const auto morphing = proc.getState().getRawParameterValue (globalParamID (gp::SceneMorphOn))->load() > 0.5f;
    if (morphing)
        return { kit.getCurrentGlobal (gp::XyX), kit.getCurrentGlobal (gp::XyY) };

    auto e = getPosition();
    if (const auto m = proc.modDisplayFor (globalParamID (gp::XyX)))
        e.x = (float) m->live;
    if (const auto m = proc.modDisplayFor (globalParamID (gp::XyY)))
        e.y = (float) m->live;
    return e;
}

void XyPad::frame()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto dt = juce::jlimit (1.0, 64.0, now - lastFrame);
    lastFrame = now;
    bool dirty = false;

    if (glide)
    {
        const auto k = easeOut ((float) ((now - glide->start) / kBase));
        const auto p = glide->from + (glide->to - glide->from) * k;
        x->setValueNotifyingHost (p.x);
        y->setValueNotifyingHost (p.y);
        if (k >= 1.0f)
        {
            x->endChangeGesture();
            y->endChangeGesture();
            glide.reset();
        }
        dirty = true;
    }

    // What the user set follows at once; what Batida moves eases in (step
    // locks snap in about 40 ms).
    const auto set = getPosition();
    if (set != shownSet)
    {
        shownSet = set;
        dirty = true;
    }
    const auto target = targetEffective();
    if (shownEffective.x < 0.0f)
        shownEffective = target;
    const auto tau = proc.getKit().isXyLocked() ? 13.0 : 16.0;
    const auto k = (float) (1.0 - std::exp (-dt / tau));
    const auto before = shownEffective;
    shownEffective += (target - shownEffective) * k;
    if (shownEffective.getDistanceFrom (target) < 1.0e-4f)
        shownEffective = target;
    if (shownEffective != before)
        dirty = true;

    const auto wantScale = grabbing ? 1.0f : 0.0f;
    if (! juce::exactlyEqual (grabScale, wantScale))
    {
        grabScale += (wantScale - grabScale) * (float) std::min (1.0, dt / kFast * 2.2);
        if (std::abs (grabScale - wantScale) < 0.01f)
            grabScale = wantScale;
        dirty = true;
    }

    // The trail: the last second of the effective position.
    if (grabbing || shownEffective != before)
        if (trail.empty() || trail.back().p.getDistanceFrom (shownEffective) > 0.004f)
            trail.push_back ({ shownEffective, now });
    while (! trail.empty() && now - trail.front().t > 1000.0)
        trail.erase (trail.begin());
    if (! trail.empty())
        dirty = true;

    if (dirty)
        repaint();
}

void XyPad::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const auto s = r.getWidth();
    g.fillAll (bg);

    for (int i = 1; i < 8; ++i)
    {
        g.setColour (i == 4 ? juce::Colour (0xff222222) : grid);
        const auto p = std::round ((float) i * s / 8.0f);
        g.fillRect (p, 0.0f, 1.0f, r.getHeight());
        g.fillRect (0.0f, p, s, 1.0f);
    }

    const auto e = getEffective();
    const auto set = getPosition();
    auto toScreen = [&] (juce::Point<float> p) { return juce::Point<float> (p.x * s, (1.0f - p.y) * r.getHeight()); };

    // Heat along the left edge.
    g.setGradientFill (juce::ColourGradient (lime.withAlpha (0.0f), 0.0f, r.getBottom(), lime.withAlpha (0.8f), 0.0f, 0.0f, false));
    g.fillRect (0.0f, (1.0f - e.y) * r.getHeight(), 3.0f, e.y * r.getHeight());

    g.setColour (juce::Colour (0xff6a6a6a));
    g.setFont (mono (9.5f));
    g.drawText ("DESTROYED", r.reduced (10.0f, 8.0f), juce::Justification::topLeft);
    g.drawText ("CLEAN", r.reduced (10.0f, 8.0f), juce::Justification::bottomLeft);

    const auto now = juce::Time::getMillisecondCounterHiRes();
    for (const auto& t : trail)
    {
        const auto a = 1.0f - (float) ((now - t.t) / 1000.0);
        g.setColour (lime.withAlpha (juce::jlimit (0.0f, 1.0f, a) * 0.55f));
        g.fillRect (juce::Rectangle<float> (4.0f, 4.0f).withCentre (toScreen (t.p)));
    }

    const auto ep = toScreen (e);
    g.setColour (lime.withAlpha (grabbing ? 0.7f : 0.35f));
    g.fillRect (std::round (ep.x), 0.0f, 1.0f, r.getHeight());
    g.fillRect (0.0f, std::round (ep.y), s, 1.0f);

    if (hover && ! grabbing)
    {
        const auto hp = toScreen (*hover);
        const float dash[] = { 2.0f, 3.0f };
        g.setColour (ink.withAlpha (0.25f));
        g.drawDashedLine ({ hp.x, 0.0f, hp.x, r.getHeight() }, dash, 2);
        g.drawDashedLine ({ 0.0f, hp.y, s, hp.y }, dash, 2);
        g.setColour (muted);
        g.setFont (mono (9.5f));
        g.drawText (juce::String (hover->x, 2) + " " + juce::String (hover->y, 2),
                    juce::Rectangle<float> (std::min (hp.x + 8.0f, s - 70.0f), std::max (hp.y - 18.0f, 22.0f), 66.0f, 12.0f),
                    juce::Justification::centredLeft);
    }

    // Where you put it (white) and where it really is (lime).
    const auto sp = toScreen (set);
    const auto apart = set.getDistanceFrom (e) > 0.004f;
    if (apart)
    {
        const float dash[] = { 2.0f, 2.0f };
        g.setColour (lime.withAlpha (0.5f));
        g.drawDashedLine ({ sp, ep }, dash, 2);
    }
    const auto size = 14.0f + 6.0f * grabScale;
    g.setColour (ink);
    g.drawRect (juce::Rectangle<float> (size, size).withCentre (sp), 1.5f);
    g.fillRect (juce::Rectangle<float> (4.0f, 4.0f).withCentre (sp));
    if (apart)
    {
        g.setColour (lime);
        g.fillRect (juce::Rectangle<float> (8.0f, 8.0f).withCentre (ep));
    }

    g.setFont (mono (9.5f));
    g.setColour (grabbing ? ink : muted);
    g.drawText ("X " + juce::String (set.x, 2) + "  Y " + juce::String (set.y, 2), r.reduced (10.0f, 8.0f), juce::Justification::topRight);
    if (apart)
    {
        g.setColour (lime);
        g.drawText (juce::String::fromUTF8 ("\xe2\x86\x92 ") + juce::String (e.x, 2) + "  " + juce::String (e.y, 2),
                    r.reduced (10.0f, 8.0f).withTrimmedTop (14.0f), juce::Justification::topRight);
    }

    g.setColour (line);
    g.drawRect (r, 1.0f);
}

void XyPad::set (float nx, float ny)
{
    nx = juce::jlimit (0.0f, 1.0f, nx);
    ny = juce::jlimit (0.0f, 1.0f, ny);
    x->setValueNotifyingHost (nx);
    y->setValueNotifyingHost (ny);
    if (onMove)
        onMove (nx, ny);
}

void XyPad::glideTo (juce::Point<float> target)
{
    if (! glide)
    {
        x->beginChangeGesture();
        y->beginChangeGesture();
    }
    glide = Glide { getPosition(), { juce::jlimit (0.0f, 1.0f, target.x), juce::jlimit (0.0f, 1.0f, target.y) },
                    juce::Time::getMillisecondCounterHiRes() - (canAnimate (*this) ? 0.0 : kBase) };
    if (! canAnimate (*this))
        frame();
}

void XyPad::mouseDown (const juce::MouseEvent& e)
{
    const auto s = (float) getWidth();
    if (e.mods.isAltDown())
    {
        glideTo ({ e.position.x / s, 1.0f - e.position.y / (float) getHeight() });
        return;
    }
    if (onGestureStart)
        onGestureStart();
    x->beginChangeGesture();
    y->beginChangeGesture();
    grabbing = true;
    grabStart = getPosition();
    mouseStart = e.position;
    hover.reset();
}

void XyPad::mouseDrag (const juce::MouseEvent& e)
{
    if (! grabbing)
        return;
    auto dx = (e.position.x - mouseStart.x) / (float) getWidth();
    auto dy = -(e.position.y - mouseStart.y) / (float) getHeight();
    if (e.mods.isShiftDown())
    {
        if (std::abs (dx) > std::abs (dy))
            dy = 0.0f;
        else
            dx = 0.0f;
    }
    set (grabStart.x + dx, grabStart.y + dy);
}

void XyPad::mouseUp (const juce::MouseEvent& e)
{
    if (! grabbing)
        return;
    grabbing = false;
    x->endChangeGesture();
    y->endChangeGesture();
    mouseMove (e);
}

void XyPad::mouseDoubleClick (const juce::MouseEvent&)
{
    glideTo ({ x->getDefaultValue(), y->getDefaultValue() });
}

void XyPad::mouseMove (const juce::MouseEvent& e)
{
    hover = juce::Point<float> (juce::jlimit (0.0f, 1.0f, e.position.x / (float) getWidth()),
                                juce::jlimit (0.0f, 1.0f, 1.0f - e.position.y / (float) getHeight()));
    repaint();
}

void XyPad::mouseExit (const juce::MouseEvent&)
{
    hover.reset();
    repaint();
}

// SceneTile ------------------------------------------------------------------------

SceneTile::SceneTile (BatidaProcessor& p, int s) : proc (p), scene (s)
{
    setRepaintsOnMouseActivity (false);
}

void SceneTile::setArmed (bool a)
{
    if (a != armed)
    {
        armed = a;
        repaint();
    }
}

void SceneTile::paint (juce::Graphics& g)
{
    const auto& sc = proc.movement().get().scenes[(size_t) scene];
    const auto r = getLocalBounds();
    const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + scene));
    const auto over = isMouseOver();

    g.setColour (armed ? lime : (over ? ink : line));
    g.drawRect (r, 1);
    g.setColour (armed ? lime : ink);
    g.setFont (mono (10.0f, Weight::Bold));
    g.drawText ("[" + letter + "]", r.withHeight (22), juce::Justification::centred);

    const auto m = std::min (r.getHeight() - 32, r.getWidth() - 24);
    const auto pad = juce::Rectangle<int> (m, m).withCentre ({ r.getCentreX(), r.getY() + 24 + m / 2 });
    if (sc.stored)
    {
        const auto px = (float) pad.getX() + sc.values[(size_t) gp::XyX] * (float) m;
        const auto py = (float) pad.getBottom() - sc.values[(size_t) gp::XyY] * (float) m;
        g.setColour (lime.withAlpha (0.35f));
        g.fillRect ((float) std::round (px), (float) pad.getY(), 1.0f, (float) m);
        g.fillRect ((float) pad.getX(), (float) std::round (py), (float) m, 1.0f);
        g.setColour (lime);
        g.fillRect (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ px, py }));
    }
    else
    {
        const float dash[] = { 2.0f, 2.0f };
        g.setColour (faint);
        const auto f = pad.toFloat();
        for (const auto& l : { juce::Line<float> (f.getTopLeft(), f.getTopRight()), juce::Line<float> (f.getTopRight(), f.getBottomRight()),
                               juce::Line<float> (f.getBottomRight(), f.getBottomLeft()), juce::Line<float> (f.getBottomLeft(), f.getTopLeft()) })
            g.drawDashedLine (l, dash, 2);
        g.setColour (muted);
        g.setFont (mono (9.0f));
        g.drawText ("empty", pad, juce::Justification::centred);
    }
}

// KitPage --------------------------------------------------------------------------

namespace
{
const char* kStageNames[] = { "01 Dynamics", "02 Distortion", "03 EQ", "04 Output" };
} // namespace

ParamControl& KitPage::add (int param, const juce::String& label)
{
    auto* c = controls.add (new ParamControl (proc.getState(), label));
    c->bind (globalParamID (param));
    addAndMakeVisible (c);
    return *c;
}

KitPage::KitPage (BatidaProcessor& p) : proc (p), pad (p)
{
    addAndMakeVisible (pad);

    compAmount = &add (gp::CompAmount, "Amount");
    compAttack = &add (gp::CompAttack, "Attack");
    stages[0] = { compAmount, compAttack, &add (gp::CompRelease, "Release"), &add (gp::CompMix, "Mix"),
                  &add (gp::CompDetector, "Detector").asMenu() };
    follow[0] = &add (gp::CompFollow, "XY");

    drive = &add (gp::DistDrive, "Drive");
    type = &add (gp::DistType, "Character");
    lowKeep = &add (gp::DistLowKeep, "Low keep");
    exciter = &add (gp::ExcAmount, "Exciter");
    tone = &add (gp::ExcTone, "Tone");
    stages[1] = { drive, type, lowKeep, exciter, tone };
    follow[1] = &add (gp::DistFollow, "XY");

    eqHigh = &add (gp::EqHigh, "High");
    stages[2] = { &add (gp::EqHp, "High-pass"), &add (gp::EqLp, "Low-pass"), &add (gp::EqLow, "Low"), &add (gp::EqMid, "Mid"), eqHigh };
    follow[2] = &add (gp::EqFollow, "XY");

    stages[3] = { &add (gp::ChainOut, "Chain out"), &add (gp::SafetyClip, "Clip") };
    follow[3] = nullptr;

    morphOn = &add (gp::SceneMorphOn, "Morph");
    morphFrom = &add (gp::SceneA, "From");
    morphTo = &add (gp::SceneB, "To");
    morph = &add (gp::SceneMorph, "Morph");
    morphFrom->withLabelWidth (34);
    morphTo->withLabelWidth (22);

    xyRec.setTooltip ("Record pad moves into the playing pattern's XY lane");
    addAndMakeVisible (xyRec);
    pad.onGestureStart = [this]
    {
        if (xyRec.getToggleState())
            proc.beginUndoStep();
    };
    pad.onMove = [this] (float x, float y)
    {
        const auto& seq = proc.sequencer();
        if (! xyRec.getToggleState() || ! seq.isRunning() || seq.getPatternStep() < 0)
            return;
        const auto pi = seq.getActivePattern();
        const auto step = seq.getPatternStep();
        proc.patterns().edit ([&] (PatternBank& b) { b.patterns[(size_t) pi].xy[(size_t) step] = { true, x, y }; });
    };

    store.setTooltip ("Store the chain into a scene: STORE, then a letter");
    store.onClick = [this]
    {
        for (auto* t : tiles)
            t->setArmed (store.getToggleState());
    };
    addAndMakeVisible (store);
    for (int i = 0; i < kNumScenes; ++i)
    {
        auto* t = tiles.add (new SceneTile (p, i));
        t->onClick = [this, i]
        {
            if (store.getToggleState())
            {
                proc.storeScene (i);
                store.setToggleState (false, juce::dontSendNotification);
                for (auto* tile : tiles)
                    tile->setArmed (false);
            }
            else if (proc.movement().get().scenes[(size_t) i].stored)
                proc.recallScene (i);
            for (auto* tile : tiles)
                tile->repaint();
        };
        addAndMakeVisible (t);
    }

    timerCallback();
    startTimerHz (30);
}

KitPage::~KitPage()
{
    stopTimer();
}

void KitPage::timerCallback()
{
    if (! isVisibleInTree (*this))
        return; // hidden pages do nothing
    if (proc.movement().getVersion() != shownMovement)
    {
        shownMovement = proc.movement().getVersion();
        for (auto* t : tiles)
            t->repaint();
    }

    // The chain as it really is: the pad's effective position (lock, morph, modulation).
    const auto base = proc.readGlobalParams();
    auto shown = base;
    const auto e = pad.getEffective();
    if (e.x >= 0.0f)
    {
        shown[gp::XyX] = e.x;
        shown[gp::XyY] = e.y;
    }
    const auto c = computeEffectiveChain (shown);
    auto marker = [] (float effective, float knob) -> std::optional<double>
    {
        if (std::abs (effective - knob) <= 1.0e-4f * std::max (1.0f, std::abs (knob)))
            return std::nullopt;
        return (double) effective;
    };
    compAmount->setMarker (marker (c.compAmount, base[gp::CompAmount]));
    compAttack->setMarker (marker (c.compAttack, base[gp::CompAttack]));
    drive->setMarker (marker (c.drive, base[gp::DistDrive]));
    type->setMarker (marker (c.type, base[gp::DistType]));
    lowKeep->setMarker (marker (c.lowKeepHz, base[gp::DistLowKeep]));
    exciter->setMarker (marker (c.excAmount, base[gp::ExcAmount]));
    tone->setMarker (marker (c.excToneHz, base[gp::ExcTone]));
    eqHigh->setMarker (marker (c.highDb, base[gp::EqHigh]));

    const auto gr = proc.getGainReductionDb();
    if (std::abs (gr - grDb) > 0.05f)
    {
        grDb = gr;
        repaint (stageBounds[0].withHeight (30));
    }
    // The zone and the readout only when the pad has moved.
    const auto zone = e.x < 1.0f / 3.0f ? 0 : e.x < 2.0f / 3.0f ? 1 : 2;
    const auto set = pad.getPosition();
    if (zone != shownZone || set != shownSet)
    {
        shownZone = zone;
        shownSet = set;
        repaint (zonesBounds);
        repaint (readoutBounds.withWidth (300));
    }
}

void KitPage::paint (juce::Graphics& g)
{
    // Zones under the pad: the one the chain is in lights up.
    const auto e = pad.getEffective();
    const auto zone = e.x < 1.0f / 3.0f ? 0 : e.x < 2.0f / 3.0f ? 1 : 2;
    const char* zones[] = { "WARM", "AGGRESSIVE", "DIGITAL" };
    const juce::Justification just[] = { juce::Justification::centredLeft, juce::Justification::centred, juce::Justification::centredRight };
    for (int i = 0; i < 3; ++i)
        drawLabel (g, zones[i], zonesBounds, i == zone ? ink : faint, just[i]);

    const auto set = pad.getPosition();
    g.setColour (muted);
    g.setFont (mono (10.0f));
    g.drawText ("character " + juce::String (juce::roundToInt (set.x * 100.0f)) + "%  " + juce::String::fromUTF8 ("\xc2\xb7")
                    + "  heat " + juce::String (juce::roundToInt (set.y * 100.0f)) + "%",
                readoutBounds, juce::Justification::centredLeft);

    // The signal path.
    g.setFont (mono (9.5f));
    g.setColour (muted);
    const auto arrow = juce::String::fromUTF8 (" \xe2\x94\x80\xe2\x96\xb6 ");
    g.drawText ("IN" + arrow + "01 DYN" + arrow + "02 DIST" + arrow + "03 EQ" + arrow + "04 OUT" + arrow + "+DRY" + arrow + "MASTER",
                flowBounds, juce::Justification::centredLeft, false);
    g.setColour (faint);
    g.drawText (juce::String::fromUTF8 ("SC \xe2\x96\xb6 01"), flowBounds, juce::Justification::centredRight, false);

    for (int s = 0; s < 4; ++s)
        drawPanel (g, stageBounds[(size_t) s], kStageNames[s]);
    if (grDb < -0.1f)
    {
        g.setColour (lime);
        g.setFont (mono (9.5f));
        g.drawText ("GR " + juce::String (grDb, 1) + " dB", stageBounds[0].reduced (8, 7).withHeight (kTagHeight).withTrimmedRight (40),
                    juce::Justification::centredRight);
    }
    drawPanel (g, scenesBounds, "05 Scenes");
}

void KitPage::resized()
{
    auto r = getLocalBounds();
    auto left = r.removeFromLeft (396);
    pad.setBounds (left.removeFromTop (396));
    left.removeFromTop (6);
    zonesBounds = left.removeFromTop (12);
    left.removeFromTop (8);
    auto readout = left.removeFromTop (20);
    xyRec.setBounds (readout.removeFromRight (64));
    readoutBounds = readout;

    r.removeFromLeft (20);
    flowBounds = r.removeFromTop (14);
    r.removeFromTop (8);
    const auto colW = (r.getWidth() - 8) / 2;
    const auto stageH = 146;
    for (int s = 0; s < 4; ++s)
    {
        stageBounds[(size_t) s] = { r.getX() + (s % 2) * (colW + 8), r.getY() + (s / 2) * (stageH + 8), colW, stageH };
        auto content = panelContent (stageBounds[(size_t) s]);
        if (follow[(size_t) s] != nullptr)
        {
            const auto w = follow[(size_t) s]->getIdealWidth();
            follow[(size_t) s]->setBounds (stageBounds[(size_t) s].getRight() - 8 - w, stageBounds[(size_t) s].getY() + 5, w, 20);
        }
        RowLayout rows { content };
        for (auto* c : stages[(size_t) s])
        {
            auto row = rows.next();
            if (c->getKind() == ParamControl::Kind::Chip)
            {
                c->setBounds (row.withTrimmedLeft (70).withWidth (c->getIdealWidth()));
                continue;
            }
            c->withValueWidth (c == type ? 92 : 70);
            c->setBounds (row);
        }
    }

    scenesBounds = r.withTop (r.getY() + 2 * stageH + 16);
    const auto w = store.getWidth() > 0 ? store.getWidth() : 70;
    store.setBounds (scenesBounds.getRight() - 8 - w, scenesBounds.getY() + 5, 70, 20);
    auto content = panelContent (scenesBounds);
    auto tileRow = content.removeFromTop (96);
    const auto tileW = (tileRow.getWidth() - 3 * 8) / 4;
    for (int i = 0; i < tiles.size(); ++i)
        tiles[i]->setBounds (tileRow.removeFromLeft (tileW).withTrimmedRight (0)), tileRow.removeFromLeft (8);
    content.removeFromTop (8);
    auto row = content.removeFromTop (20);
    morphOn->setBounds (row.removeFromLeft (morphOn->getIdealWidth()));
    row.removeFromLeft (14);
    morphFrom->setBounds (row.removeFromLeft (morphFrom->getIdealWidth()));
    row.removeFromLeft (14);
    morphTo->setBounds (row.removeFromLeft (morphTo->getIdealWidth()));
    content.removeFromTop (4);
    morph->setBounds (content.removeFromTop (20));
}
