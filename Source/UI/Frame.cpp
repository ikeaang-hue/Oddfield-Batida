#include "Frame.h"

using namespace theme;
using namespace batida;

// SoundCell ------------------------------------------------------------------------

SoundCell::SoundCell (int i) : index (i)
{
    setRepaintsOnMouseActivity (true);
}

void SoundCell::setData (const Data& d)
{
    const auto changed = d.name != data.name || d.detail != data.detail || d.selected != data.selected || d.missing != data.missing
                      || d.audible != data.audible || d.mute != data.mute || d.solo != data.solo
                      || std::abs (d.chain - data.chain) > 0.01f;
    data = d;
    if (changed)
        repaint();
}

void SoundCell::hit()
{
    lastHit = juce::Time::getMillisecondCounterHiRes();
    if (vblank == nullptr)
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] (double)
        {
            if (juce::Time::getMillisecondCounterHiRes() - lastHit < kDecay + 40.0)
                repaint (lightArea().expanded (2));
        });
    repaint (lightArea().expanded (2));
}

void SoundCell::setDropHighlight (bool on)
{
    if (on != dropHighlight)
    {
        dropHighlight = on;
        repaint();
    }
}

juce::Rectangle<int> SoundCell::lightArea() const
{
    return { getWidth() - 14, 7, 7, 7 };
}

juce::Rectangle<int> SoundCell::soloArea() const
{
    return { getWidth() - 16, getHeight() - 18, 12, 14 };
}

juce::Rectangle<int> SoundCell::muteArea() const
{
    return soloArea().translated (-14, 0);
}

void SoundCell::paint (juce::Graphics& g)
{
    const auto sel = data.selected;
    const auto fg = sel ? bg : ink;
    const auto dim = sel ? bg.withAlpha (0.55f) : muted;
    if (sel)
        g.fillAll (ink);
    else if (isMouseOver (true))
        g.fillAll (raised);

    // Name
    g.setColour (data.audible ? fg : fg.withAlpha (0.35f));
    g.setFont (mono (10.5f, Weight::Bold));
    g.drawText (juce::String (index + 1).paddedLeft ('0', 2) + " " + data.name.toUpperCase(),
                getLocalBounds().reduced (7, 0).withTrimmedRight (14).withHeight (21).withTrimmedTop (5), juce::Justification::centredLeft, true);

    // Hit light, or "!" when the sample is missing.
    const auto light = lightArea();
    if (data.missing)
    {
        const auto tag = light.expanded (2, 2).withTrimmedLeft (-1);
        g.setColour (sel ? bg : ink);
        g.fillRect (tag);
        g.setColour (sel ? ink : bg);
        g.setFont (mono (9.0f, Weight::Bold));
        g.drawText ("!", tag, juce::Justification::centred);
    }
    else
    {
        const auto age = juce::Time::getMillisecondCounterHiRes() - lastHit;
        const auto a = age < kDecay ? std::pow (1.0f - (float) (age / kDecay), 2.0f) : 0.0f;
        g.setColour (sel ? juce::Colour (0xff999999) : faint);
        g.fillRect (light);
        if (a > 0.0f)
        {
            g.setColour (lime.withAlpha (0.25f + 0.75f * a));
            g.fillRect (light);
        }
    }

    // Second line: note or track length, Chain amount, M and S.
    auto line2 = getLocalBounds().reduced (7, 0).withTop (getHeight() - 18).withHeight (14);
    g.setColour (dim);
    g.setFont (mono (9.5f));
    g.drawText (data.detail, line2.removeFromLeft (46), juce::Justification::centredLeft, false);

    const auto blocks = juce::roundToInt (data.chain * 6.0f);
    auto chainArea = juce::Rectangle<int> (line2.getX() + 4, line2.getCentreY() - 4, 6 * 5, 8);
    for (int i = 0; i < 6; ++i)
    {
        const auto b = juce::Rectangle<int> (chainArea.getX() + i * 5, chainArea.getY(), 4, 8);
        g.setColour (i < blocks ? (sel ? bg : ink) : (sel ? bg.withAlpha (0.2f) : faint));
        if (i < blocks)
            g.fillRect (b);
        else
            g.drawRect (b, 1);
    }

    for (auto [area, on, text] : { std::tuple { muteArea(), data.mute, "M" }, std::tuple { soloArea(), data.solo, "S" } })
    {
        if (on)
        {
            g.setColour (sel ? bg : ink);
            g.fillRect (area);
        }
        g.setColour (on ? (sel ? ink : bg) : dim);
        g.setFont (on ? mono (9.5f, Weight::Bold) : mono (9.5f));
        g.drawText (text, area, juce::Justification::centred, false);
    }

    if (dropHighlight)
    {
        g.setColour (lime);
        g.drawRect (getLocalBounds(), 2);
    }
}

void SoundCell::mouseDown (const juce::MouseEvent& e)
{
    dragStarted = false;
    pressed = false;
    if (e.mods.isPopupMenu())
    {
        if (onMenu)
            onMenu();
        return;
    }
    if (muteArea().expanded (2).contains (e.getPosition()))
    {
        if (onMute)
            onMute();
        return;
    }
    if (soloArea().expanded (2).contains (e.getPosition()))
    {
        if (onSolo)
            onSolo();
        return;
    }
    pressed = true;
    if (onPress)
        onPress();
}

void SoundCell::mouseDrag (const juce::MouseEvent& e)
{
    if (! pressed || dragStarted || e.getDistanceFromDragStart() < 8)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragStarted = true;
        juce::Image image (juce::Image::ARGB, getWidth(), getHeight(), true);
        {
            juce::Graphics g (image);
            g.setColour (ink.withAlpha (0.85f));
            g.fillRect (image.getBounds());
            g.setColour (bg);
            g.setFont (mono (10.5f, Weight::Bold));
            g.drawText (juce::String (index + 1).paddedLeft ('0', 2) + " " + data.name.toUpperCase(), image.getBounds().reduced (7, 0),
                        juce::Justification::centredLeft);
        }
        container->startDragging ("voice:" + juce::String (index), this, juce::ScaledImage (image));
    }
}

void SoundCell::mouseUp (const juce::MouseEvent&)
{
    if (pressed && onRelease)
        onRelease();
    pressed = false;
}

void SoundCell::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! muteArea().expanded (2).contains (e.getPosition()) && ! soloArea().expanded (2).contains (e.getPosition()) && onRename)
        onRename();
}

bool SoundCell::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith ("voice:");
}

void SoundCell::itemDropped (const SourceDetails& d)
{
    setDropHighlight (false);
    const auto from = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
    if (onSwapFrom && from != index)
        onSwapFrom (from);
}

juce::PopupMenu resampleMenu (BatidaProcessor& proc, int target, int pattern)
{
    juce::PopupMenu m;
    const auto empty = proc.patterns().get().patterns[(size_t) pattern].isEmpty();
    m.addItem (kResamplePatternItem, "P" + juce::String (pattern + 1).paddedLeft ('0', 2) + " " + proc.getPatternName (pattern) + " (loop)", ! empty);
    m.addSeparator();
    for (int v = 0; v < kNumVoices; ++v)
        m.addItem (kResampleSoundItem + v, juce::String (v + 1).paddedLeft ('0', 2) + " " + proc.getVoiceName (v) + (v == target ? " (itself)" : ""));
    return m;
}

bool runResample (BatidaProcessor& proc, int choice, int target, int pattern)
{
    const auto isPattern = choice == kResamplePatternItem;
    const auto isSound = choice >= kResampleSoundItem && choice < kResampleSoundItem + kNumVoices;
    if (! isPattern && ! isSound)
        return false;
    juce::String error;
    juce::MouseCursor::showWaitCursor();
    const auto ok = isPattern ? proc.resamplePattern (pattern, target, &error) // the pattern the menu named
                              : proc.resampleSound (choice - kResampleSoundItem, target, &error);
    juce::MouseCursor::hideWaitCursor();
    if (! ok)
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Couldn't resample").withMessage (error).withButton ("OK"), nullptr);
    return true;
}

void refreshSoundCells (BatidaProcessor& proc, std::array<SoundCell*, kNumVoices> cells, int selected,
                        const std::function<juce::String (int)>& detail)
{
    bool anySolo = false;
    std::array<VoiceParams, kNumVoices> p;
    for (int v = 0; v < kNumVoices; ++v)
    {
        p[(size_t) v] = proc.readVoiceParams (v);
        anySolo = anySolo || p[(size_t) v].flag (vp::Solo);
    }
    for (int v = 0; v < kNumVoices; ++v)
    {
        SoundCell::Data d;
        d.name = proc.getVoiceName (v);
        d.detail = detail (v);
        d.selected = v == selected;
        d.missing = proc.sampleSlot (v).getStatus() == SampleSlot::Status::Missing;
        d.mute = p[(size_t) v].flag (vp::Mute);
        d.solo = p[(size_t) v].flag (vp::Solo);
        d.audible = ! d.mute && (! anySolo || d.solo);
        d.chain = p[(size_t) v][vp::ChainAmt];
        cells[(size_t) v]->setData (d);
    }
}

// SoundRow -------------------------------------------------------------------------

SoundRow::SoundRow()
{
    for (int v = 0; v < kNumVoices; ++v)
        addAndMakeVisible (cells.add (new SoundCell (v)));
}

std::array<SoundCell*, kNumVoices> SoundRow::getCells()
{
    std::array<SoundCell*, kNumVoices> a {};
    for (int v = 0; v < kNumVoices; ++v)
        a[(size_t) v] = cells[v];
    return a;
}

void SoundRow::resized()
{
    auto r = getLocalBounds().reduced (1);
    const auto w = r.getWidth() / kNumVoices;
    for (int v = 0; v < kNumVoices; ++v)
        cells[v]->setBounds (v == kNumVoices - 1 ? r : r.removeFromLeft (w));
}

void SoundRow::paint (juce::Graphics& g)
{
    g.setColour (line);
    g.drawRect (getLocalBounds(), 1);
    for (int v = 1; v < kNumVoices; ++v)
        g.fillRect (cells[v]->getX() - 1, 0, 1, getHeight());
}

// Meter ----------------------------------------------------------------------------

void Meter::setLevel (float peak)
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto db = juce::Decibels::gainToDecibels (peak, -60.0f);
    const auto level = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
    // Rises at once, falls by about a block per 60 ms.
    const auto fallen = shown - (float) ((now - lastUpdate) / 600.0);
    lastUpdate = now;
    const auto next = std::max (level, fallen);
    if (std::abs (next - shown) > 1.0e-3f)
    {
        shown = std::max (0.0f, next);
        repaint();
    }
}

void Meter::paint (juce::Graphics& g)
{
    const auto lit = juce::roundToInt (shown * 10.0f);
    const auto w = (getWidth() - 9) / 10;
    for (int i = 0; i < 10; ++i)
    {
        const juce::Rectangle<int> b (i * (w + 1), getHeight() / 2 - 5, w, 10);
        if (i < lit)
        {
            g.setColour (i >= 9 ? ink : lime);
            g.fillRect (b);
        }
        else
        {
            g.setColour (faint);
            g.drawRect (b, 1);
        }
    }
}

// TopBar ---------------------------------------------------------------------------

namespace
{
const char* kPages[] = { "KIT", "SEQ", "MOD", "SOUND", "LIB" };
constexpr int kNavX = 110;
} // namespace

TopBar::TopBar (BatidaProcessor& p) : master (p.getState())
{
    master.numberOnly().withLabelWidth (0);
    master.bind (globalParamID (gp::Master));
    master.setTooltip ("Master level");
    kitStrip.setPrefix ("kit: ");
    kitStrip.setComponentID ("kitStrip");
    notice.onClick = [this] { if (onRelink) onRelink(); };
    settings.onClick = [this] { if (onSettings) onSettings(); };
    undo.setComponentID ("undo");
    redo.setComponentID ("redo");
    for (juce::Component* c : { (juce::Component*) &kitStrip, (juce::Component*) &undo, (juce::Component*) &redo,
                                (juce::Component*) &settings, (juce::Component*) &master, (juce::Component*) &meter })
        addAndMakeVisible (c);
    addChildComponent (notice);
}

void TopBar::setPage (int p)
{
    if (p != page)
    {
        page = p;
        repaint (navBounds());
    }
}

void TopBar::setMissing (int count)
{
    notice.count = count;
    notice.repaint();
    if (notice.isVisible() != (count > 0))
    {
        notice.setVisible (count > 0);
        resized();
    }
}

juce::Rectangle<int> TopBar::navItem (int i) const
{
    int x = kNavX;
    for (int k = 0; k < 5; ++k)
    {
        const auto w = (int) juce::GlyphArrangement::getStringWidth (mono (10.5f, Weight::Bold), kPages[k]) + 16;
        if (k == i)
            return { x, getHeight() / 2 - 13, w, 26 };
        x += w;
    }
    return {};
}

juce::Rectangle<int> TopBar::navBounds() const
{
    return navItem (0).getUnion (navItem (4));
}

void TopBar::paint (juce::Graphics& g)
{
    g.setColour (ink);
    g.setFont (head (12.5f, true));
    g.drawText ("BATIDA/", juce::Rectangle<int> (0, 0, 100, getHeight()), juce::Justification::centredLeft, false);

    const auto mouse = isMouseOver() ? getMouseXYRelative() : juce::Point<int> (-1, -1);
    for (int i = 0; i < 5; ++i)
    {
        const auto r = navItem (i);
        const auto on = i == page;
        if (on)
        {
            g.setColour (ink);
            g.fillRect (r);
        }
        g.setColour (on ? bg : (r.contains (mouse) ? ink : muted));
        g.setFont (on ? mono (10.5f, Weight::Bold) : mono (10.5f));
        g.drawText (kPages[i], r, juce::Justification::centred, false);
    }
    g.setColour (line);
    g.drawRect (navBounds(), 1);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
}

void TopBar::resized()
{
    auto r = getLocalBounds();
    auto right = r.removeFromRight (300);
    master.setBounds (right.removeFromRight (60).withSizeKeepingCentre (60, 20));
    right.removeFromRight (6);
    meter.setBounds (right.removeFromRight (70).withSizeKeepingCentre (70, 12));
    right.removeFromRight (14);
    settings.setBounds (right.removeFromRight (30).withSizeKeepingCentre (30, 20));
    redo.setBounds (right.removeFromRight (40).withSizeKeepingCentre (40, 20));
    undo.setBounds (right.removeFromRight (40).withSizeKeepingCentre (40, 20));

    // The kit strip sits in the middle; with missing samples, the notice
    // comes first and the strip moves over.
    auto middle = juce::Rectangle<int>::leftTopRightBottom (navBounds().getRight() + 16, 0, undo.getX() - 12, getHeight());
    if (notice.isVisible())
    {
        notice.setBounds (middle.removeFromLeft (184).withSizeKeepingCentre (184, 20));
        middle.removeFromLeft (8);
    }
    else
        middle = juce::Rectangle<int> (0, 0, 210, getHeight()).withCentre ({ getWidth() / 2 + 40, getHeight() / 2 });
    kitStrip.setBounds (middle.withSizeKeepingCentre (std::min (210, middle.getWidth()), 24));
}

juce::Rectangle<int> TopBar::logoBounds() const
{
    return { 0, 0, (int) juce::GlyphArrangement::getStringWidth (head (12.5f, true), "BATIDA/") + 4, getHeight() };
}

void TopBar::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (logoBounds().contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint (navBounds());
}

void TopBar::mouseDown (const juce::MouseEvent& e)
{
    if (logoBounds().contains (e.getPosition()))
    {
        if (onAbout)
            onAbout();
        return;
    }
    for (int i = 0; i < 5; ++i)
        if (navItem (i).contains (e.getPosition()))
        {
            if (onPage)
                onPage (i);
            return;
        }
}

void TopBar::Notice::paintButton (juce::Graphics& g, bool over, bool)
{
    const auto r = getLocalBounds();
    g.setColour (over ? lime : ink);
    g.fillRect (r);
    g.setColour (bg);
    g.setFont (mono (9.5f, Weight::Bold));
    const auto text = "! " + juce::String (count) + " SAMPLE" + (count == 1 ? "" : "S") + " MISSING";
    auto t = r.reduced (6, 0);
    g.drawText (text, t, juce::Justification::centredLeft, false);
    g.setFont (mono (9.5f));
    g.drawText (juce::String::fromUTF8 ("RELINK\xe2\x80\xa6"), t, juce::Justification::centredRight, false);
}

// SettingsPanel --------------------------------------------------------------------

SettingsPanel::SettingsPanel (BatidaProcessor& p) : proc (p), midiMode (p.getState()), keysSound (p.getState())
{
    midiMode.withLabelWidth (0).bind (globalParamID (gp::MidiMode));
    keysSound.withLabelWidth (0).asSegmented().bind (globalParamID (gp::KeysVoice));
    zoom.setSelected (p.library().getZoom() >= 150 ? 2 : p.library().getZoom() >= 125 ? 1 : 0);
    zoom.onChange = [this] (int i)
    {
        const auto percent = i == 2 ? 150 : i == 1 ? 125 : 100;
        proc.library().setZoom (percent);
        if (onZoom)
            onZoom (percent);
    };
    reveal.onClick = [this] { proc.library().getRoot().revealToUser(); };
    author.setText (p.library().getAuthor(), false);
    author.setTextToShowWhenEmpty ("your name, for saved files", faint);
    author.setFont (mono (10.5f));
    author.setIndents (8, 5);
    author.onTextChange = [this] { proc.library().setAuthor (author.getText().trim()); };
    author.onReturnKey = [this] { author.giveAwayKeyboardFocus(); };
    for (juce::Component* c : { (juce::Component*) &midiMode, (juce::Component*) &keysSound, (juce::Component*) &zoom,
                                (juce::Component*) &reveal, (juce::Component*) &author })
        addAndMakeVisible (c);
    refresh();
}

void SettingsPanel::refresh()
{
    const auto chromatic = proc.getState().getParameter (globalParamID (gp::MidiMode))->getValue() > 0.5f;
    keysSound.setAlpha (chromatic ? 1.0f : 0.4f);
}

void SettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (ink);
    g.drawRect (getLocalBounds(), 1);
    auto r = getLocalBounds().reduced (14);
    drawTag (g, r.removeFromTop (kTagHeight).withWidth (200), "Settings");

    auto label = [&] (juce::Component& c, const juce::String& text, int above = 16)
    {
        drawLabel (g, text, juce::Rectangle<int> (14, c.getY() - above, 200, 12));
    };
    label (midiMode, "MIDI mode");
    label (keysSound, "Keys sound");
    label (zoom, "Zoom");
    label (reveal, "Library", 34);
    label (author, "Author");

    g.setColour (ink);
    g.setFont (mono (10.0f));
    g.drawText (proc.library().getRoot().getFullPathName().replace (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName(), "~"),
                juce::Rectangle<int> (14, reveal.getY() - 20, getWidth() - 28, 14), juce::Justification::centredLeft, true);
}

void SettingsPanel::resized()
{
    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (kTagHeight + 26);
    midiMode.setBounds (r.removeFromTop (22).withWidth (200));
    r.removeFromTop (26);
    keysSound.setBounds (r.removeFromTop (22).withWidth (208));
    r.removeFromTop (26);
    zoom.setBounds (r.removeFromTop (22).withWidth (zoom.getIdealWidth()));
    r.removeFromTop (44);
    reveal.setBounds (r.removeFromTop (22).withWidth (120));
    r.removeFromTop (26);
    author.setBounds (r.removeFromTop (26));
}

// AboutCard ------------------------------------------------------------------------

void AboutCard::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (ink);
    g.drawRect (getLocalBounds(), 1);
    auto r = getLocalBounds().reduced (18, 16);

    g.setFont (head (22.0f, true));
    g.drawText ("BATIDA/", r.removeFromTop (28), juce::Justification::centredLeft, false);
    g.setColour (muted);
    g.setFont (mono (10.5f));
    g.drawText ("by Negative Space", r.removeFromTop (16), juce::Justification::centredLeft, false);
    r.removeFromTop (14);
    g.setColour (line);
    g.fillRect (r.removeFromTop (1));
    r.removeFromTop (12);

    auto row = [&] (const juce::String& label, const juce::String& value, const juce::String& detail = {})
    {
        auto area = r.removeFromTop (detail.isNotEmpty() ? 30 : 16);
        drawLabel (g, label, area.removeFromLeft (82).withHeight (16));
        g.setColour (ink);
        g.setFont (mono (10.5f, Weight::Medium));
        g.drawText (value, area.withHeight (16), juce::Justification::centredLeft, true);
        if (detail.isNotEmpty())
        {
            g.setColour (muted);
            g.setFont (mono (9.5f));
            g.drawText (detail, area.withTrimmedTop (15).withHeight (14), juce::Justification::centredLeft, true);
        }
        r.removeFromTop (8);
    };
    row ("Version", BATIDA_VERSION);
    row ("Licence", "GNU AGPL v3", "free and open source");
    row ("Fonts", "JetBrains Mono, Martian Mono", "SIL Open Font License 1.1");
    row ("Built with", "JUCE " + juce::String (JUCE_MAJOR_VERSION) + "." + juce::String (JUCE_MINOR_VERSION) + "."
                           + juce::String (JUCE_BUILDNUMBER));
    r.removeFromTop (4);
    g.setColour (line);
    g.fillRect (r.removeFromTop (1));
    r.removeFromTop (10);
    g.setColour (muted);
    g.setFont (mono (9.5f));
    g.drawText (juce::String::fromUTF8 ("\xc2\xa9 2026 Negative Space"), r.removeFromTop (14), juce::Justification::centredLeft, false);
}
