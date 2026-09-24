#include "ParamControl.h"

#include "Plugin/PluginProcessor.h"

using namespace theme;

namespace
{
BatidaProcessor* processorOf (juce::AudioProcessorValueTreeState& state)
{
    return dynamic_cast<BatidaProcessor*> (&state.processor);
}

constexpr int kGap = 8;
constexpr float kFineFactor = 0.1f;
} // namespace

ParamControl::ParamControl (juce::AudioProcessorValueTreeState& s, juce::String labelText)
    : state (s), fixedLabel (std::move (labelText))
{
    setRepaintsOnMouseActivity (false);
    setWantsKeyboardFocus (false);
}

ParamControl::~ParamControl()
{
    stopTimer();
}

void ParamControl::bind (const juce::String& paramID)
{
    attachment.reset();
    param = state.getParameter (paramID);
    jassert (param != nullptr);
    if (param == nullptr)
        return;
    boundID = paramID;

    // "S3 Op2 Ratio" -> "Op2 Ratio": the sound is shown by the selection.
    auto name = param->getName (64);
    if (name.length() > 2 && (name[0] == 'S' || name[0] == 'V') && juce::CharacterFunctions::isDigit (name[1]) && name.containsChar (' '))
        name = name.fromFirstOccurrenceOf (" ", false, false);
    label = fixedLabel.isNotEmpty() ? fixedLabel : name;

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
        kind = forceMenu || (choice->choices.size() > 5 && ! forceSegmented) ? Kind::Menu : Kind::Segmented;
    else if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
        kind = Kind::Chip;
    else
        kind = onlyNumber ? Kind::Number : Kind::Bar;

    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { valueChanged (v); }, nullptr);
    shownIndex = -1.0f;
    chipFill = -1.0f;
    attachment->sendInitialUpdate();

    modulation.reset();
    if (kind == Kind::Bar && isModulatable())
    {
        timerCallback();
        startTimerHz (30);
    }
    else
        stopTimer();
    repaint();
}

void ParamControl::setLabelText (const juce::String& text)
{
    fixedLabel = text;
    if (label != text)
    {
        label = text;
        repaint();
    }
}

void ParamControl::setMarker (std::optional<double> value)
{
    if (value != marker)
    {
        marker = value;
        repaint();
    }
}

bool ParamControl::isModulatable() const
{
    auto* proc = processorOf (state);
    return proc != nullptr && proc->isModulatable (boundID);
}

void ParamControl::timerCallback()
{
    auto* proc = processorOf (state);
    if (proc == nullptr)
        return;
    std::optional<Modulation> m;
    if (const auto d = proc->modDisplayFor (boundID))
        m = Modulation { normOf (d->live), normOf (d->low), normOf (d->high) };
    const auto changed = m.has_value() != modulation.has_value()
                      || (m && (std::abs (m->live - modulation->live) > 1.0e-4f || std::abs (m->low - modulation->low) > 1.0e-4f
                                 || std::abs (m->high - modulation->high) > 1.0e-4f));
    if (changed)
    {
        modulation = m;
        repaint();
    }
}

// Values -------------------------------------------------------------------------

float ParamControl::norm() const
{
    return param != nullptr ? param->getValue() : 0.0f;
}

float ParamControl::normOf (double value) const
{
    return param != nullptr ? juce::jlimit (0.0f, 1.0f, param->convertTo0to1 ((float) value)) : 0.0f;
}

juce::String ParamControl::textFor (float n) const
{
    return param != nullptr ? param->getText (n, 32) : juce::String();
}

juce::StringArray ParamControl::options() const
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
    {
        if (optionLabels.size() == choice->choices.size())
            return optionLabels;
        return choice->choices;
    }
    return {};
}

void ParamControl::valueChanged (float)
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (kind == Kind::Segmented || kind == Kind::Menu)
    {
        auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param);
        const auto index = (float) (choice != nullptr ? choice->getIndex() : 0);
        if (shownIndex < 0.0f)
            shownIndex = index;
        else if (! juce::exactlyEqual (index, shownIndex))
        {
            slideFrom = shownIndex;
            shownIndex = index;
            slideStart = now;
            animate();
        }
    }
    else if (kind == Kind::Chip)
    {
        const auto on = norm() >= 0.5f;
        if (chipFill < 0.0f)
        {
            chipFill = on ? 1.0f : 0.0f;
            chipTarget = on;
        }
        else if (on != chipTarget)
        {
            chipTarget = on;
            chipStart = now;
            animate();
        }
    }
    repaint();
}

// Geometry -----------------------------------------------------------------------

juce::Rectangle<int> ParamControl::valueArea() const
{
    return getLocalBounds().removeFromRight (onlyNumber ? getWidth() : valueWidth);
}

juce::Rectangle<int> ParamControl::trackArea() const
{
    auto r = getLocalBounds();
    r.removeFromLeft (labelWidth > 0 ? labelWidth + kGap : 0);
    r.removeFromRight (valueWidth + kGap);
    return r.withSizeKeepingCentre (r.getWidth(), 8);
}

juce::Rectangle<int> ParamControl::controlArea() const
{
    auto r = getLocalBounds();
    if (kind != Kind::Chip && labelWidth > 0 && label.isNotEmpty())
        r.removeFromLeft (labelWidth + kGap);
    return r;
}

int ParamControl::getIdealWidth() const
{
    const auto font = mono (10.0f, Weight::Bold);
    if (kind == Kind::Chip)
        return (int) juce::GlyphArrangement::getStringWidth (font, "[" + label.toUpperCase() + "]") + 12;
    int w = 0;
    for (const auto& o : options())
        w += (int) juce::GlyphArrangement::getStringWidth (font, o.toUpperCase()) + 14;
    return w + (labelWidth > 0 && label.isNotEmpty() ? labelWidth + kGap : 0);
}

juce::Rectangle<int> ParamControl::optionBounds (int index) const
{
    const auto opts = options();
    const auto area = controlArea();
    const auto font = mono (10.0f, Weight::Bold);
    std::vector<int> widths;
    int total = 0;
    for (const auto& o : opts)
    {
        widths.push_back ((int) juce::GlyphArrangement::getStringWidth (font, o.toUpperCase()) + 14);
        total += widths.back();
    }
    // Natural widths when they fit; otherwise shared equally.
    int x = area.getX();
    for (int i = 0; i < (int) widths.size(); ++i)
    {
        const auto w = total <= area.getWidth() ? widths[(size_t) i] : area.getWidth() / std::max (1, (int) widths.size());
        if (i == index)
            return { x, area.getY(), w, area.getHeight() };
        x += w;
    }
    return {};
}

int ParamControl::optionAt (int x) const
{
    const auto n = options().size();
    for (int i = 0; i < n; ++i)
        if (x < optionBounds (i).getRight())
            return i;
    return n - 1;
}

// Painting -----------------------------------------------------------------------

void ParamControl::paint (juce::Graphics& g)
{
    if (param == nullptr)
        return;
    const auto over = isMouseOver (true) && isEnabled();

    if (kind == Kind::Bar || kind == Kind::Number)
    {
        const auto n = norm();
        const auto moved = marker.has_value() && std::abs (normOf (*marker) - n) > 0.002f;
        const auto modded = modulation.has_value();

        if (kind == Kind::Bar)
        {
            if (labelWidth > 0)
                drawLabel (g, label, getLocalBounds().removeFromLeft (labelWidth), over || dragging ? ink : muted);

            const auto t = trackArea().toFloat();
            g.setColour (over || dragging ? trackHover : track);
            g.fillRect (t);

            // Bipolar parameters (range crosses zero) fill from zero.
            const auto& range = param->getNormalisableRange();
            const auto zero = range.start < 0.0f && range.end > 0.0f ? juce::jlimit (0.0f, 1.0f, range.convertTo0to1 (0.0f)) : 0.0f;
            if (zero > 0.0f)
            {
                g.setColour (faint);
                g.fillRect (t.getX() + zero * t.getWidth() - 0.5f, t.getY() - 2.0f, 1.0f, t.getHeight() + 4.0f);
            }
            const auto a = std::min (zero, n), b = std::max (zero, n);
            g.setColour (ink);
            g.fillRect (t.getX() + a * t.getWidth(), t.getY(), std::max (1.0f, (b - a) * t.getWidth()), t.getHeight());

            if (modded)
            {
                g.setColour (limeBand);
                g.fillRect (t.getX() + modulation->low * t.getWidth(), t.getY(), (modulation->high - modulation->low) * t.getWidth(), t.getHeight());
            }
            if (moved)
            {
                const auto e = normOf (*marker);
                g.setColour (limeSoft);
                g.fillRect (t.getX() + std::min (n, e) * t.getWidth(), t.getY(), std::abs (e - n) * t.getWidth(), t.getHeight());
                g.setColour (lime);
                g.fillRect (t.getX() + e * t.getWidth() - 1.0f, t.getY() - 3.0f, 2.0f, t.getHeight() + 6.0f);
            }
            if (modded)
            {
                g.setColour (lime);
                g.fillRect (t.getX() + modulation->live * t.getWidth() - 1.0f, t.getY() - 3.0f, 2.0f, t.getHeight() + 6.0f);
            }
            if (defaultFlash > 0.0f)
            {
                g.setColour (ink.withAlpha (defaultFlash));
                g.fillRect (t.getX() + param->getDefaultValue() * t.getWidth() - 0.5f, t.getY() - 4.0f, 1.0f, t.getHeight() + 8.0f);
            }
            if (dragging && fine)
            {
                const juce::Rectangle<float> tag (t.getRight() - 26.0f, t.getY() - 1.0f, 26.0f, t.getHeight() + 2.0f);
                g.setColour (lime);
                g.fillRect (tag);
                g.setColour (bg);
                g.setFont (mono (7.5f, Weight::Bold));
                g.drawText ("FINE", tag, juce::Justification::centred);
            }
        }

        if (kind == Kind::Number && labelWidth > 0 && label.isNotEmpty())
            drawLabel (g, label, getLocalBounds().removeFromLeft (labelWidth), over || dragging ? ink : muted);

        // The number: lime when Batida moves it, inverted while you drag.
        auto v = valueArea();
        juce::String text;
        if (modded)
            text = textFor (modulation->live);
        else
            text = textFor (moved ? normOf (*marker) : n);
        const auto font = mono (10.5f, Weight::Medium);
        if (dragging)
        {
            const auto w = (int) juce::GlyphArrangement::getStringWidth (font, text) + 10;
            const auto box = v.withLeft (v.getRight() - w).withSizeKeepingCentre (w, 16);
            g.setColour (ink);
            g.fillRect (box);
            g.setColour (bg);
            g.setFont (font);
            g.drawText (text, box, juce::Justification::centred, false);
        }
        else
        {
            g.setColour ((moved || modded) ? lime : ink);
            g.setFont (font);
            g.drawText (text, v.withTrimmedRight (5), juce::Justification::centredRight, false);
        }
        return;
    }

    if (kind == Kind::Chip)
    {
        const auto w = std::min (getWidth(), getIdealWidth());
        const auto r = getLocalBounds().withWidth (w).withSizeKeepingCentre (w, std::min (getHeight(), 20)).toFloat().reduced (0.5f);
        const auto on = norm() >= 0.5f;
        const auto fill = juce::jlimit (0.0f, 1.0f, chipFill);
        if (fill > 0.0f)
        {
            g.setColour (lime);
            g.fillRect (r.withWidth (r.getWidth() * fill));
        }
        g.setColour (on ? lime : (over ? ink : faint));
        g.drawRect (r, 1.0f);
        g.setFont (mono (9.5f, Weight::Bold));
        const auto text = label.toUpperCase();
        g.setColour (fill > 0.5f ? bg : muted);
        g.drawText (on ? text : "[" + text + "]", r, juce::Justification::centred, false);
        return;
    }

    // Segmented and menu rows: an optional label on the left.
    if (labelWidth > 0 && label.isNotEmpty())
        drawLabel (g, label, getLocalBounds().removeFromLeft (labelWidth), over ? ink : muted);

    const auto opts = options();
    auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param);
    const auto index = choice != nullptr ? choice->getIndex() : 0;
    const auto area = controlArea().withSizeKeepingCentre (controlArea().getWidth(), std::min (controlArea().getHeight(), 20));

    if (kind == Kind::Menu)
    {
        auto r = area.toFloat().reduced (0.5f);
        g.setColour (over ? ink : line);
        g.drawRect (r, 1.0f);
        g.setColour (ink);
        g.setFont (mono (10.5f));
        g.drawText (index < opts.size() ? opts[index] : juce::String(), r.reduced (6.0f, 0.0f).withTrimmedRight (12.0f),
                    juce::Justification::centredLeft, true);
        g.setColour (muted);
        g.drawText (juce::String::fromUTF8 ("\xe2\x96\xbe"), r.removeFromRight (16.0f), juce::Justification::centred);
        return;
    }

    // Segmented: the white block slides to the chosen option.
    const auto last = optionBounds (opts.size() - 1);
    const auto outline = juce::Rectangle<int>::leftTopRightBottom (area.getX(), area.getY(), last.getRight(), area.getBottom());
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto k = canAnimate (*this) ? easeOut ((float) ((now - slideStart) / kSlide)) : 1.0f;
    auto blockFrom = optionBounds ((int) std::round (slideFrom)).withY (area.getY()).withHeight (area.getHeight());
    auto blockTo = optionBounds (index).withY (area.getY()).withHeight (area.getHeight());
    const auto block = k >= 1.0f ? blockTo.toFloat()
                                 : juce::Rectangle<float> ((float) blockFrom.getX() + ((float) blockTo.getX() - (float) blockFrom.getX()) * k,
                                                           (float) area.getY(),
                                                           (float) blockFrom.getWidth() + ((float) blockTo.getWidth() - (float) blockFrom.getWidth()) * k,
                                                           (float) area.getHeight());
    g.setColour (ink);
    g.fillRect (block);
    for (int i = 0; i < opts.size(); ++i)
    {
        const auto ob = optionBounds (i).withY (area.getY()).withHeight (area.getHeight());
        const auto inBlock = block.contains (ob.toFloat().getCentre());
        g.setColour (inBlock ? bg : muted);
        g.setFont (inBlock ? mono (10.0f, Weight::Bold) : mono (10.0f));
        g.drawText (opts[i].toUpperCase(), ob, juce::Justification::centred, false);
        if (i > 0)
        {
            g.setColour (line);
            g.fillRect (ob.getX(), ob.getY(), 1, ob.getHeight());
        }
    }
    g.setColour (line);
    g.drawRect (outline, 1);
}

// Animation ----------------------------------------------------------------------

void ParamControl::animate()
{
    if (vblank == nullptr)
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] (double) { animate(); });

    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto live = canAnimate (*this);
    bool busy = false;

    if (glide)
    {
        const auto k = live ? easeOut ((float) ((now - glide->start) / kBase)) : 1.0f;
        attachment->setValueAsPartOfGesture (param->convertFrom0to1 (glide->from + (glide->to - glide->from) * k));
        if (k >= 1.0f)
        {
            attachment->endGesture();
            glide.reset();
        }
        busy = true;
    }
    if (now - slideStart < kSlide + 20.0)
    {
        repaint();
        busy = true;
    }
    if (kind == Kind::Chip)
    {
        const auto k = live ? easeOut ((float) ((now - chipStart) / kWipe)) : 1.0f;
        const auto target = chipTarget ? 1.0f : 0.0f;
        if (std::abs (chipFill - target) > 1.0e-3f)
        {
            chipFill = chipTarget ? k : 1.0f - k;
            repaint();
            busy = busy || k < 1.0f;
        }
    }
    if (defaultFlash > 0.0f)
    {
        defaultFlash = std::max (0.0f, 1.0f - (float) ((now - flashStart) / 300.0));
        repaint();
        busy = true;
    }
    juce::ignoreUnused (busy);
}

void ParamControl::glideTo (float target)
{
    if (param == nullptr || attachment == nullptr)
        return;
    if (glide)
        attachment->endGesture();
    attachment->beginGesture();
    glide = Glide { norm(), juce::jlimit (0.0f, 1.0f, target), juce::Time::getMillisecondCounterHiRes() };
    animate();
}

// Mouse --------------------------------------------------------------------------

void ParamControl::mouseDown (const juce::MouseEvent& e)
{
    if (param == nullptr || attachment == nullptr || ! isEnabled())
        return;

    if (kind == Kind::Segmented)
    {
        if (e.mods.isPopupMenu())
            return;
        attachment->setValueAsCompleteGesture ((float) optionAt (e.x));
        return;
    }
    if (kind == Kind::Menu)
    {
        showChoiceMenu();
        return;
    }
    if (kind == Kind::Chip)
    {
        if (! e.mods.isPopupMenu())
            attachment->setValueAsCompleteGesture (norm() >= 0.5f ? 0.0f : 1.0f);
        return;
    }

    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    if (e.mods.isAltDown())
    {
        glideTo (param->getDefaultValue());
        defaultFlash = 1.0f;
        flashStart = juce::Time::getMillisecondCounterHiRes();
        return;
    }
    if (glide)
    {
        attachment->endGesture();
        glide.reset();
    }
    dragging = true;
    fine = e.mods.isShiftDown();
    pressedOnValue = valueArea().contains (e.getPosition());
    movedSincePress = false;
    dragStartNorm = norm();
    dragOrigin = lastMouse = e.position;
    attachment->beginGesture();
    repaint();
}

void ParamControl::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    lastMouse = e.position;
    if (e.getDistanceFromDragStart() > 2)
        movedSincePress = true;
    if (e.mods.isShiftDown() != fine)
    {
        // Pressing or releasing Shift mid-drag never jumps: carry on from here.
        fine = e.mods.isShiftDown();
        dragStartNorm = norm();
        dragOrigin = e.position;
    }
    const auto width = (float) std::max (60, kind == Kind::Bar ? trackArea().getWidth() : 160);
    const auto delta = ((e.position.x - dragOrigin.x) - (e.position.y - dragOrigin.y)) / width;
    const auto n = juce::jlimit (0.0f, 1.0f, dragStartNorm + delta * (fine ? kFineFactor : 1.0f));
    attachment->setValueAsPartOfGesture (param->convertFrom0to1 (n));
}

void ParamControl::modifierKeysChanged (const juce::ModifierKeys& mods)
{
    if (dragging && mods.isShiftDown() != fine)
    {
        fine = mods.isShiftDown();
        dragStartNorm = norm();
        dragOrigin = lastMouse;
        repaint();
    }
}

void ParamControl::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    dragging = false;
    fine = false;
    attachment->endGesture();
    repaint();

    // A click on the number (no drag) opens a field to type into, unless it
    // turns out to be a double-click (which goes back to the default).
    if (pressedOnValue && ! movedSincePress && e.getNumberOfClicks() == 1)
    {
        const auto token = ++clickToken;
        juce::Timer::callAfterDelay (juce::MouseEvent::getDoubleClickTimeout() + 20,
                                     [safe = juce::Component::SafePointer<ParamControl> (this), token]
                                     {
                                         if (safe != nullptr && safe->clickToken == token)
                                             safe->editValue();
                                     });
    }
}

void ParamControl::mouseDoubleClick (const juce::MouseEvent&)
{
    if ((kind != Kind::Bar && kind != Kind::Number) || param == nullptr)
        return;
    ++clickToken; // not a click to type after all
    glideTo (param->getDefaultValue());
    defaultFlash = 1.0f;
    flashStart = juce::Time::getMillisecondCounterHiRes();
}

void ParamControl::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if ((kind != Kind::Bar && kind != Kind::Number) || param == nullptr || attachment == nullptr)
    {
        Component::mouseWheelMove (e, wheel);
        return;
    }
    const auto amount = (wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX) * (wheel.isReversed ? -1.0f : 1.0f);
    const auto step = e.mods.isShiftDown() ? 0.002f : 0.02f;
    attachment->setValueAsCompleteGesture (param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, norm() + (amount > 0 ? step : -step))));
}

void ParamControl::showMenu()
{
    auto* proc = processorOf (state);
    juce::PopupMenu m;
    m.addSectionHeader (label + "  " + textFor (norm()));
    const auto canModulate = proc != nullptr && proc->isModulatable (boundID);
    if (canModulate)
    {
        for (int mod = 0; mod < batida::kNumMods; ++mod)
            m.addItem (mod + 1, "Modulate with Mod " + juce::String (mod + 1), true, proc->isModTarget (mod, boundID));
        m.addSeparator();
    }
    juce::PopupMenu::Item type ("Type a value...");
    type.itemID = 10;
    type.shortcutKeyDescription = "click the number";
    m.addItem (type);
    juce::PopupMenu::Item reset ("Back to default");
    reset.itemID = 11;
    reset.shortcutKeyDescription = "double-click";
    m.addItem (reset);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                     [safe = juce::Component::SafePointer<ParamControl> (this), proc, id = boundID] (int choice)
                     {
                         if (safe == nullptr || choice == 0)
                             return;
                         if (choice <= batida::kNumMods && proc != nullptr)
                             proc->toggleModTarget (choice - 1, id);
                         else if (choice == 10)
                             safe->editValue();
                         else if (choice == 11)
                         {
                             safe->glideTo (safe->param->getDefaultValue());
                             safe->defaultFlash = 1.0f;
                             safe->flashStart = juce::Time::getMillisecondCounterHiRes();
                         }
                     });
}

void ParamControl::showChoiceMenu()
{
    auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param);
    if (choice == nullptr)
        return;
    juce::PopupMenu m;
    const auto opts = options();
    for (int i = 0; i < opts.size(); ++i)
        m.addItem (i + 1, opts[i], true, i == choice->getIndex());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (controlArea().getWidth()),
                     [safe = juce::Component::SafePointer<ParamControl> (this)] (int picked)
                     {
                         if (safe != nullptr && picked > 0 && safe->attachment != nullptr)
                             safe->attachment->setValueAsCompleteGesture ((float) (picked - 1));
                     });
}

void ParamControl::editValue()
{
    if (param == nullptr)
        return;
    editor = std::make_unique<juce::TextEditor>();
    auto* ed = editor.get();
    ed->setFont (mono (10.5f, Weight::Medium));
    ed->setColour (juce::TextEditor::backgroundColourId, ink);
    ed->setColour (juce::TextEditor::textColourId, bg);
    ed->setColour (juce::TextEditor::highlightColourId, bg.withAlpha (0.2f));
    ed->setColour (juce::TextEditor::highlightedTextColourId, bg);
    ed->setColour (juce::TextEditor::outlineColourId, ink);
    ed->setColour (juce::TextEditor::focusedOutlineColourId, ink);
    ed->setColour (juce::CaretComponent::caretColourId, bg);
    ed->setJustification (juce::Justification::centredRight);
    ed->setIndents (4, 2);
    ed->setText (textFor (norm()), false);
    ed->setBounds (valueArea().withSizeKeepingCentre (valueArea().getWidth(), 18));
    addAndMakeVisible (*ed);

    auto finish = [this, ed] (bool keep)
    {
        if (editor.get() != ed)
            return;
        if (keep && param != nullptr)
        {
            const auto text = ed->getText().trim();
            if (text.isNotEmpty())
                glideTo (param->getValueForText (text));
        }
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ParamControl> (this), ed]
        {
            if (safe != nullptr && safe->editor.get() == ed)
                safe->editor.reset();
        });
    };
    ed->onReturnKey = [finish] { finish (true); };
    ed->onFocusLost = [finish] { finish (true); };
    ed->onEscapeKey = [finish] { finish (false); };
    ed->selectAll();
    ed->grabKeyboardFocus();
}
