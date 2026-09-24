#include "ModPage.h"

using namespace batida;
using namespace theme;

namespace
{
juce::String targetName (BatidaProcessor& proc, const ModTarget& t)
{
    if (t.global)
        return globalParamSpecs()[(size_t) t.param].label;
    return juce::String (t.voice + 1) + " " + proc.getVoiceName (t.voice) + " " + voiceParamSpecs()[(size_t) t.param].label;
}
} // namespace

// ShapeEditor ----------------------------------------------------------------------

ShapeEditor::ShapeEditor (BatidaProcessor& p, int m) : proc (p), mod (m)
{
    setTooltip ("Click: add a point. Drag: move it. Double-click: delete it. Option-drag a segment: bend it.");
    startTimerHz (30);
}

juce::Point<float> ShapeEditor::toScreen (float x, float y) const
{
    const auto r = area();
    return { r.getX() + x * r.getWidth(), r.getBottom() - y * r.getHeight() };
}

int ShapeEditor::pointAt (juce::Point<float> p) const
{
    const auto& s = proc.movement().get().mods[(size_t) mod];
    for (int i = 0; i < s.numPoints; ++i)
        if (toScreen (s.points[(size_t) i].x, s.points[(size_t) i].y).getDistanceFrom (p) < 8.0f)
            return i;
    return -1;
}

int ShapeEditor::segmentAt (float x) const
{
    const auto& s = proc.movement().get().mods[(size_t) mod];
    for (int i = s.numPoints - 1; i >= 0; --i)
        if (x >= s.points[(size_t) i].x)
            return i;
    return s.numPoints - 1;
}

void ShapeEditor::paint (juce::Graphics& g)
{
    const auto frame = getLocalBounds().toFloat();
    g.fillAll (bg);
    const auto r = area();
    for (int i = 1; i < 16; ++i)
    {
        g.setColour (i % 4 == 0 ? juce::Colour (0xff333333) : juce::Colour (0xff1f1f1f));
        g.fillRect (r.getX() + r.getWidth() * (float) i / 16.0f, frame.getY(), 1.0f, frame.getHeight());
    }
    const float dash[] = { 2.0f, 3.0f };
    g.setColour (juce::Colour (0xff1a1a1a));
    g.drawDashedLine ({ r.getX(), r.getCentreY(), r.getRight(), r.getCentreY() }, dash, 2);

    const auto& s = proc.movement().get().mods[(size_t) mod];
    juce::Path curve;
    for (int i = 0; i <= 240; ++i)
    {
        const auto x = (float) i / 240.0f;
        const auto p = toScreen (x, evaluateShape (s, std::min (x, 0.9999f)));
        i == 0 ? curve.startNewSubPath (p) : curve.lineTo (p);
    }
    auto fill = curve;
    fill.lineTo (r.getRight(), r.getBottom());
    fill.lineTo (r.getX(), r.getBottom());
    fill.closeSubPath();
    g.setColour (lime.withAlpha (0.08f));
    g.fillPath (fill);
    g.setColour (lime);
    g.strokePath (curve, juce::PathStrokeType (1.6f));

    // Curve handles halfway along each segment; points as squares.
    for (int i = 0; i < s.numPoints; ++i)
    {
        const auto x0 = s.points[(size_t) i].x;
        const auto x1 = i + 1 < s.numPoints ? s.points[(size_t) i + 1].x : 1.0f;
        const auto xm = 0.5f * (x0 + x1);
        const auto h = juce::Rectangle<float> (6.5f, 6.5f).withCentre (toScreen (xm, evaluateShape (s, std::min (xm, 0.9999f))));
        g.setColour (bg);
        g.fillEllipse (h);
        g.setColour (lime);
        g.drawEllipse (h, 1.2f);
    }
    for (int i = 0; i < s.numPoints; ++i)
    {
        g.setColour (i == dragPoint ? ink : lime);
        g.fillRect (juce::Rectangle<float> (7.0f, 7.0f).withCentre (toScreen (s.points[(size_t) i].x, s.points[(size_t) i].y)));
    }

    // Where the modulator is now, and its value.
    const auto phase = proc.getKit().getModulators().getUiPhase (mod);
    const auto value = evaluateShape (s, std::min (phase, 0.9999f));
    const auto p = toScreen (phase, value);
    g.setColour (ink);
    g.fillRect (p.x, frame.getY(), 1.0f, frame.getHeight());
    g.fillRect (juce::Rectangle<float> (6.0f, 6.0f).withCentre (p));
    g.setFont (mono (9.5f, Weight::Medium));
    g.drawText (juce::String (juce::roundToInt (value * 100.0f)) + "%",
                juce::Rectangle<float> (std::min (p.x + 6.0f, frame.getRight() - 40.0f), frame.getY() + 6.0f, 36.0f, 12.0f),
                juce::Justification::centredLeft);

    g.setColour (line);
    g.drawRect (frame, 1.0f);
}

void ShapeEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto r = area();
    const auto nx = juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());
    const auto ny = juce::jlimit (0.0f, 1.0f, (r.getBottom() - e.position.y) / r.getHeight());

    if (e.mods.isAltDown())
    {
        curveSegment = segmentAt (nx);
        curveStart = proc.movement().get().mods[(size_t) mod].points[(size_t) curveSegment].curve;
        return;
    }

    dragPoint = pointAt (e.position);
    if (dragPoint >= 0)
        return;

    // Add a point where you clicked, in order.
    proc.movement().edit ([&] (MovementData& d)
    {
        auto& s = d.mods[(size_t) mod];
        if (s.numPoints >= kMaxModPoints)
            return;
        int at = s.numPoints;
        for (int i = 0; i < s.numPoints; ++i)
            if (nx < s.points[(size_t) i].x)
            {
                at = i;
                break;
            }
        at = std::max (1, at); // the first point stays at the start
        for (int i = s.numPoints; i > at; --i)
            s.points[(size_t) i] = s.points[(size_t) i - 1];
        s.points[(size_t) at] = { nx, ny, 0.0f };
        ++s.numPoints;
        dragPoint = at;
    });
}

void ShapeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (curveSegment >= 0)
    {
        const auto c = juce::jlimit (-1.0f, 1.0f, curveStart - (float) e.getDistanceFromDragStartY() / 80.0f);
        proc.movement().edit ([&] (MovementData& d) { d.mods[(size_t) mod].points[(size_t) curveSegment].curve = c; });
        return;
    }
    if (dragPoint < 0)
        return;

    const auto r = area();
    proc.movement().edit ([&] (MovementData& d)
    {
        auto& s = d.mods[(size_t) mod];
        auto& p = s.points[(size_t) dragPoint];
        const auto lo = dragPoint == 0 ? 0.0f : s.points[(size_t) dragPoint - 1].x + 0.001f;
        const auto hi = dragPoint == 0 ? 0.0f : (dragPoint + 1 < s.numPoints ? s.points[(size_t) dragPoint + 1].x - 0.001f : 0.999f);
        p.x = juce::jlimit (lo, std::max (lo, hi), (e.position.x - r.getX()) / r.getWidth());
        p.y = juce::jlimit (0.0f, 1.0f, (r.getBottom() - e.position.y) / r.getHeight());
    });
}

void ShapeEditor::mouseUp (const juce::MouseEvent&)
{
    dragPoint = curveSegment = -1;
}

void ShapeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto i = pointAt (e.position);
    if (i <= 0) // the first point can't be deleted
        return;
    proc.movement().edit ([&] (MovementData& d)
    {
        auto& s = d.mods[(size_t) mod];
        if (s.numPoints <= 2)
            return;
        for (int k = i; k < s.numPoints - 1; ++k)
            s.points[(size_t) k] = s.points[(size_t) k + 1];
        --s.numPoints;
    });
    dragPoint = -1;
}

// DepthBar -------------------------------------------------------------------------

void DepthBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    drawLabel (g, name, r.removeFromLeft (150), isMouseOver() || dragging ? ink : muted);
    r.removeFromLeft (8);
    auto value = r.removeFromRight (50);
    r.removeFromRight (8);
    const auto t = r.withSizeKeepingCentre (r.getWidth(), 8).toFloat();
    g.setColour (isMouseOver() || dragging ? trackHover : track);
    g.fillRect (t);
    g.setColour (faint);
    g.fillRect (t.getCentreX() - 0.5f, t.getY() - 2.0f, 1.0f, t.getHeight() + 4.0f);
    const auto a = std::min (0.5f, 0.5f + depth * 0.5f), b = std::max (0.5f, 0.5f + depth * 0.5f);
    g.setColour (ink);
    g.fillRect (t.getX() + a * t.getWidth(), t.getY(), std::max (1.0f, (b - a) * t.getWidth()), t.getHeight());
    const auto text = (depth > 0.005f ? "+" : "") + juce::String (juce::roundToInt (depth * 100.0f)) + "%";
    const auto font = mono (10.5f, Weight::Medium);
    if (dragging)
    {
        const auto w = (int) juce::GlyphArrangement::getStringWidth (font, text) + 10;
        const auto box = value.withLeft (value.getRight() - w).withSizeKeepingCentre (w, 16);
        g.setColour (ink);
        g.fillRect (box);
        g.setColour (bg);
        g.setFont (font);
        g.drawText (text, box, juce::Justification::centred);
        return;
    }
    g.setColour (ink);
    g.setFont (font);
    g.drawText (text, value.withTrimmedRight (5), juce::Justification::centredRight);
}

void DepthBar::mouseDown (const juce::MouseEvent&)
{
    dragging = true;
    start = depth;
    repaint();
}

void DepthBar::mouseDrag (const juce::MouseEvent& e)
{
    const auto width = (float) std::max (60, getWidth() - 216);
    const auto fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
    depth = juce::jlimit (-1.0f, 1.0f, start + fine * 2.0f * (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / width);
    if (onChange)
        onChange (depth);
    repaint();
}

// ModPanel -------------------------------------------------------------------------

ParamControl& ModPanel::add (int field, const juce::String& label)
{
    auto* c = controls.add (new ParamControl (proc.getState(), label));
    c->bind (globalParamID (modParam (mod, field)));
    addAndMakeVisible (c);
    return *c;
}

ModPanel::ModPanel (BatidaProcessor& p, int m) : proc (p), mod (m), shape (p, m)
{
    addAndMakeVisible (shape);
    shape.setComponentID ("shape" + juce::String (m + 1));

    mode = &add (ModMode, {});
    mode->withLabelWidth (0);
    rate = &add (ModRate, "Rate");
    rate->asMenu();
    freeRate = &add (ModHz, "Free rate");
    trigger = &add (ModTrigger, "Fire on");
    trigger->asMenu();
    polarity = &add (ModPolarity, "Polarity");
    polarity->withOptionLabels ({ "Uni", "Bi" });
    smooth = &add (ModSmooth, "Smooth");
    humanise = &add (ModHuman, "Humanise");
    amount = &add (ModAmount, "Amount");

    shapes.onClick = [this]
    {
        juce::PopupMenu menu;
        static const char* names[] = { "Sine", "Triangle", "Ramp up", "Ramp down", "Square", "Random steps" };
        for (int i = 0; i < 6; ++i)
            menu.addItem (i + 1, names[i]);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (shapes), [this] (int choice)
        {
            if (choice > 0)
                proc.movement().edit ([&] (MovementData& d) { setPreset (d.mods[(size_t) mod], (ShapePreset) (choice - 1)); });
        });
    };
    addAndMakeVisible (shapes);
    addTarget.onClick = [this] { showAddMenu(); };
    addAndMakeVisible (addTarget);

    for (int i = 0; i < kMaxModTargets; ++i)
    {
        auto* d = depths.add (new DepthBar());
        d->onChange = [this, i] (float v)
        {
            const auto slot = slotOf[(size_t) i];
            proc.movement().edit ([&] (MovementData& data) { data.mods[(size_t) mod].targets[(size_t) slot].depth = v; });
        };
        auto* rm = removes.add (new TextLink (juce::String::fromUTF8 ("\xc3\x97")));
        rm->setTooltip ("Remove this target");
        rm->onClick = [this, i]
        {
            const auto slot = slotOf[(size_t) i];
            proc.movement().edit ([&] (MovementData& data) { data.mods[(size_t) mod].targets[(size_t) slot] = {}; });
        };
        addChildComponent (d);
        addChildComponent (rm);
    }
    rebuildTargets();
    timerCallback();
    startTimerHz (10);
}

ModPanel::~ModPanel()
{
    stopTimer();
}

void ModPanel::timerCallback()
{
    // Dim what the current mode doesn't use: Sync and One-shot use the rate,
    // Free uses Hz, only One-shot fires on hits.
    const auto m = juce::roundToInt (proc.readGlobalParams()[(size_t) modParam (mod, ModMode)]);
    rate->setAlpha (m == 1 ? 0.4f : 1.0f);
    freeRate->setAlpha (m == 1 ? 1.0f : 0.4f);
    trigger->setAlpha (m == 2 ? 1.0f : 0.4f);
    if (proc.movement().getVersion() != shownVersion || proc.getNamesVersion() != shownNames)
        rebuildTargets();
}

void ModPanel::rebuildTargets()
{
    shownVersion = proc.movement().getVersion();
    shownNames = proc.getNamesVersion();
    const auto& targets = proc.movement().get().mods[(size_t) mod].targets;
    rows = 0;
    for (int slot = 0; slot < kMaxModTargets; ++slot)
    {
        const auto& t = targets[(size_t) slot];
        if (! t.active)
            continue;
        const auto row = rows++;
        slotOf[(size_t) row] = slot;
        depths[row]->setLabel (targetName (proc, t));
        if (! depths[row]->isMouseButtonDown())
            depths[row]->setDepth (t.depth);
    }
    for (int i = 0; i < kMaxModTargets; ++i)
    {
        depths[i]->setVisible (i < rows);
        removes[i]->setVisible (i < rows);
    }
    addTarget.setEnabled (rows < kMaxModTargets);
    repaint (targetsHeader);
}

void ModPanel::showAddMenu()
{
    juce::PopupMenu kitMenu;
    std::vector<juce::String> ids;
    auto addItem = [&] (juce::PopupMenu& menu, const juce::String& id, const juce::String& name)
    {
        ids.push_back (id);
        menu.addItem ((int) ids.size(), name, true, proc.isModTarget (mod, id));
    };
    for (int g = 0; g < kNumGlobalParams; ++g)
        if (isModulatableGlobal (g))
            addItem (kitMenu, globalParamID (g), globalParamSpecs()[(size_t) g].label);

    juce::PopupMenu m;
    m.addSubMenu ("Kit (pad and chain)", kitMenu);
    for (int v = 0; v < kNumVoices; ++v)
    {
        juce::PopupMenu soundMenu;
        for (int p = 0; p < kNumVoiceParams; ++p)
            if (isModulatableVoice (p))
                addItem (soundMenu, voiceParamID (v, p), voiceParamSpecs()[(size_t) p].label);
        m.addSubMenu (juce::String (v + 1) + " " + proc.getVoiceName (v), soundMenu);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addTarget),
                     [this, ids] (int choice)
                     {
                         if (choice > 0 && choice <= (int) ids.size())
                             proc.toggleModTarget (mod, ids[(size_t) choice - 1]);
                     });
}

void ModPanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds(), juce::String (mod + 1).paddedLeft ('0', 2) + " Mod " + juce::String (mod + 1));
    g.setColour (line);
    g.fillRect (separator1);
    g.fillRect (separator2);
    g.setColour (ink);
    g.setFont (head (8.5f));
    g.drawText ("TARGETS " + juce::String::fromUTF8 ("\xc2\xb7 ") + juce::String (rows), targetsHeader, juce::Justification::centredLeft);
}

void ModPanel::resized()
{
    auto header = getLocalBounds().reduced (8, 5).removeFromTop (20);
    mode->setBounds (header.removeFromRight (mode->getIdealWidth()));
    header.removeFromRight (8);
    shapes.setBounds (header.removeFromRight (84));

    auto r = panelContent (getLocalBounds());
    shape.setBounds (r.removeFromTop (150));
    r.removeFromTop (10);
    const auto colW = (r.getWidth() - 16) / 2;
    auto left = r.withWidth (colW), right = r.withTrimmedLeft (colW + 16);
    rate->setBounds (left.removeFromTop (20).withWidth (colW));
    left.removeFromTop (2);
    trigger->setBounds (left.removeFromTop (20));
    freeRate->setBounds (right.removeFromTop (20));
    right.removeFromTop (2);
    polarity->setBounds (right.removeFromTop (20).withWidth (polarity->getIdealWidth()));
    r.removeFromTop (48);
    separator1 = r.removeFromTop (1);
    r.removeFromTop (8);
    for (auto* c : { smooth, humanise, amount })
    {
        c->setBounds (r.removeFromTop (20));
        r.removeFromTop (2);
    }
    r.removeFromTop (6);
    separator2 = r.removeFromTop (1);
    r.removeFromTop (8);
    auto th = r.removeFromTop (22);
    addTarget.setBounds (th.removeFromRight (108));
    targetsHeader = th;
    r.removeFromTop (4);
    for (int i = 0; i < kMaxModTargets; ++i)
    {
        auto row = r.removeFromTop (20);
        removes[i]->setBounds (row.removeFromRight (20));
        row.removeFromRight (4);
        depths[i]->setBounds (row);
        r.removeFromTop (2);
    }
}

// ModPage --------------------------------------------------------------------------

ModPage::ModPage (BatidaProcessor& p) : one (p, 0), two (p, 1)
{
    addAndMakeVisible (one);
    addAndMakeVisible (two);
}

void ModPage::resized()
{
    auto r = getLocalBounds();
    one.setBounds (r.removeFromLeft ((r.getWidth() - 10) / 2));
    r.removeFromLeft (10);
    two.setBounds (r);
}
