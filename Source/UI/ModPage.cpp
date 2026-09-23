#include "ModPage.h"

using namespace batida;

namespace
{
const juce::Colour kModColour (0xff6fd08c);

juce::String targetName (BatidaProcessor& proc, const ModTarget& t)
{
    if (t.global)
        return globalParamSpecs()[(size_t) t.param].label;
    return juce::String (t.voice + 1) + " " + proc.getVoiceName (t.voice) + ": " + voiceParamSpecs()[(size_t) t.param].label;
}
} // namespace

// ShapeEditor -------------------------------------------------------------------

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
    const auto r = area();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    for (int i = 1; i < 4; ++i)
        g.drawVerticalLine ((int) (r.getX() + r.getWidth() * i / 4.0f), r.getY(), r.getBottom());
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    const auto& s = proc.movement().get().mods[(size_t) mod];
    juce::Path curve;
    for (int i = 0; i <= 200; ++i)
    {
        const auto x = i / 200.0f;
        const auto p = toScreen (x, evaluateShape (s, std::min (x, 0.9999f)));
        i == 0 ? curve.startNewSubPath (p) : curve.lineTo (p);
    }
    auto fill = curve;
    fill.lineTo (r.getRight(), r.getBottom());
    fill.lineTo (r.getX(), r.getBottom());
    fill.closeSubPath();
    g.setColour (kModColour.withAlpha (0.15f));
    g.fillPath (fill);
    g.setColour (kModColour);
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    for (int i = 0; i < s.numPoints; ++i)
    {
        const auto p = toScreen (s.points[(size_t) i].x, s.points[(size_t) i].y);
        g.setColour (i == dragPoint ? juce::Colours::white : kModColour);
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (p));
    }

    // Where the modulator is now.
    const auto phase = proc.getKit().getModulators().getUiPhase (mod);
    const auto x = r.getX() + phase * r.getWidth();
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
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
        const auto c = juce::jlimit (-1.0f, 1.0f, curveStart - e.getDistanceFromDragStartY() / 80.0f);
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

// TargetList --------------------------------------------------------------------

TargetList::TargetList (BatidaProcessor& p, int m) : proc (p), mod (m)
{
    addAndMakeVisible (addButton);
    addButton.onClick = [this] { showAddMenu(); };

    for (int i = 0; i < kMaxModTargets; ++i)
    {
        auto& d = depths[(size_t) i];
        d.setSliderStyle (juce::Slider::LinearBar);
        d.setRange (-1.0, 1.0, 0.01);
        d.setTextValueSuffix ("");
        d.textFromValueFunction = [] (double v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100)) + "%"; };
        d.setColour (juce::Slider::trackColourId, kModColour.withAlpha (0.5f));
        d.onValueChange = [this, i]
        {
            const auto slot = slotOf[(size_t) i];
            const auto v = (float) depths[(size_t) i].getValue();
            proc.movement().edit ([&] (MovementData& data) { data.mods[(size_t) mod].targets[(size_t) slot].depth = v; });
        };
        auto& rm = removeButtons[(size_t) i];
        rm.setButtonText ("x");
        rm.setTooltip ("Remove this target");
        rm.onClick = [this, i]
        {
            const auto slot = slotOf[(size_t) i];
            proc.movement().edit ([&] (MovementData& data) { data.mods[(size_t) mod].targets[(size_t) slot] = {}; });
        };
        names[(size_t) i].setFont (juce::FontOptions (12.0f));
        names[(size_t) i].setMinimumHorizontalScale (0.6f);
        addChildComponent (names[(size_t) i]);
        addChildComponent (d);
        addChildComponent (rm);
    }
    rebuild();
    startTimerHz (10);
}

void TargetList::timerCallback()
{
    if (proc.movement().getVersion() != shownVersion || proc.getNamesVersion() != shownNames)
        rebuild();
}

void TargetList::rebuild()
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
        names[(size_t) row].setText (targetName (proc, t), juce::dontSendNotification);
        if (! depths[(size_t) row].isMouseButtonDown())
            depths[(size_t) row].setValue (t.depth, juce::dontSendNotification);
    }
    for (int i = 0; i < kMaxModTargets; ++i)
    {
        names[(size_t) i].setVisible (i < rows);
        depths[(size_t) i].setVisible (i < rows);
        removeButtons[(size_t) i].setVisible (i < rows);
    }
    addButton.setEnabled (rows < kMaxModTargets);
    repaint();
}

void TargetList::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (22);
    addButton.setBounds (header.removeFromRight (110).reduced (0, 1));
    const auto rowH = 15;
    for (int i = 0; i < kMaxModTargets; ++i)
    {
        auto row = r.removeFromTop (rowH);
        removeButtons[(size_t) i].setBounds (row.removeFromRight (20).reduced (1));
        depths[(size_t) i].setBounds (row.removeFromRight (110).reduced (2, 1));
        names[(size_t) i].setBounds (row);
    }
}

void TargetList::paint (juce::Graphics& g)
{
    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Targets", getLocalBounds().removeFromTop (22), juce::Justification::centredLeft);
    if (rows == 0)
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Right-click any knob > Modulate with Mod " + juce::String (mod + 1) + ", or Add target",
                    getLocalBounds().withTrimmedTop (24).removeFromTop (20), juce::Justification::centredLeft);
    }
}

void TargetList::showAddMenu()
{
    juce::PopupMenu kitMenu;
    std::vector<juce::String> ids;
    auto add = [&] (juce::PopupMenu& menu, const juce::String& id, const juce::String& name)
    {
        ids.push_back (id);
        menu.addItem ((int) ids.size(), name, true, proc.isModTarget (mod, id));
    };
    for (int g = 0; g < kNumGlobalParams; ++g)
        if (isModulatableGlobal (g))
            add (kitMenu, globalParamID (g), globalParamSpecs()[(size_t) g].label);

    juce::PopupMenu m;
    m.addSubMenu ("Kit (pad and chain)", kitMenu);
    for (int v = 0; v < kNumVoices; ++v)
    {
        juce::PopupMenu voiceMenu;
        for (int p = 0; p < kNumVoiceParams; ++p)
            if (isModulatableVoice (p))
                add (voiceMenu, voiceParamID (v, p), voiceParamSpecs()[(size_t) p].label);
        m.addSubMenu (juce::String (v + 1) + " " + proc.getVoiceName (v), voiceMenu);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addButton),
                     [this, ids] (int choice)
                     {
                         if (choice > 0 && choice <= (int) ids.size())
                             proc.toggleModTarget (mod, ids[(size_t) choice - 1]);
                     });
}

// ModPanel ----------------------------------------------------------------------

ModPanel::ModPanel (BatidaProcessor& p, int m) : VoicePage (p), mod (m), shape (p, m), targets (p, m)
{
    title.setText ("Mod " + juce::String (m + 1), juce::dontSendNotification);
    title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, kModColour);
    addAndMakeVisible (title);
    addAndMakeVisible (shape);
    addAndMakeVisible (targets);
    shape.setComponentID ("shape" + juce::String (m + 1));
    addAndMakeVisible (shapeButton);

    shapeButton.onClick = [this]
    {
        juce::PopupMenu menu;
        static const char* names[] = { "Sine", "Triangle", "Ramp up", "Ramp down", "Square", "Random steps" };
        for (int i = 0; i < 6; ++i)
            menu.addItem (i + 1, names[i]);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (shapeButton), [this] (int choice)
        {
            if (choice > 0)
                proc.movement().edit ([&] (MovementData& d) { setPreset (d.mods[(size_t) mod], (ShapePreset) (choice - 1)); });
        });
    };

    rateSection = addSection ("Timing");
    addGlobal (rateSection, modParam (m, ModMode), "Mode").withWidth (96);
    syncRate = &addGlobal (rateSection, modParam (m, ModRate), "Sync Rate").withWidth (84);
    freeRate = &addGlobal (rateSection, modParam (m, ModHz), "Free Rate");
    trigger = &addGlobal (rateSection, modParam (m, ModTrigger), "One-shot On").withWidth (110);

    feelSection = addSection ("Output");
    addGlobal (feelSection, modParam (m, ModPolarity), "Polarity").withWidth (96);
    addGlobal (feelSection, modParam (m, ModSmooth), "Smooth");
    addGlobal (feelSection, modParam (m, ModHuman), "Humanise");
    addGlobal (feelSection, modParam (m, ModAmount), "Amount");

    timerCallback();
    startTimerHz (5);
}

void ModPanel::timerCallback()
{
    // Dim what the current mode doesn't use.
    const auto mode = juce::roundToInt (proc.readGlobalParams()[(size_t) modParam (mod, ModMode)]);
    syncRate->setAlpha (mode == 1 ? 0.35f : 1.0f);   // Sync and One-shot use the sync rate
    freeRate->setAlpha (mode == 1 ? 1.0f : 0.35f);   // Free uses Hz
    trigger->setAlpha (mode == 2 ? 1.0f : 0.35f);
}

void ModPanel::paint (juce::Graphics& g)
{
    g.setColour (juce::Colours::white.withAlpha (0.03f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    VoicePage::paint (g);
}

void ModPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (24);
    title.setBounds (top.removeFromLeft (80));
    shapeButton.setBounds (top.removeFromRight (80).reduced (0, 1));
    r.removeFromTop (4);
    shape.setBounds (r.removeFromTop (98));
    r.removeFromTop (6);
    r.removeFromTop (layoutRow (r, { rateSection }) + 4);
    r.removeFromTop (layoutRow (r, { feelSection }) + 4);
    targets.setBounds (r);
}

// ModPage -----------------------------------------------------------------------

ModPage::ModPage (BatidaProcessor& p) : one (p, 0), two (p, 1)
{
    addAndMakeVisible (one);
    addAndMakeVisible (two);
}

void ModPage::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2d31));
}

void ModPage::resized()
{
    auto r = getLocalBounds().reduced (6);
    one.setBounds (r.removeFromLeft (r.getWidth() / 2).reduced (4, 0));
    two.setBounds (r.reduced (4, 0));
}
