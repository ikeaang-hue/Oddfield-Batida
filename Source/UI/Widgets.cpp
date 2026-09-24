#include "Widgets.h"

using namespace theme;

// Segmented ----------------------------------------------------------------------

Segmented::Segmented (juce::StringArray o, int s) : options (std::move (o)), selected (s), from (s)
{
    setRepaintsOnMouseActivity (true);
}

void Segmented::setOptions (juce::StringArray o)
{
    options = std::move (o);
    selected = juce::jlimit (0, std::max (0, options.size() - 1), selected);
    from = selected;
    repaint();
}

void Segmented::setSelected (int index, bool notify)
{
    index = juce::jlimit (0, std::max (0, options.size() - 1), index);
    if (index != selected)
    {
        from = selected;
        selected = index;
        slideStart = juce::Time::getMillisecondCounterHiRes();
        if (vblank == nullptr)
            vblank = std::make_unique<juce::VBlankAttachment> (this, [this] (double)
            {
                if (juce::Time::getMillisecondCounterHiRes() - slideStart < kSlide + 20.0)
                    repaint();
            });
        repaint();
    }
    if (notify && onChange)
        onChange (selected);
}

int Segmented::getIdealWidth() const
{
    int w = 0;
    for (const auto& o : options)
        w += (int) juce::GlyphArrangement::getStringWidth (mono (10.0f, Weight::Bold), o.toUpperCase()) + 14;
    return w;
}

juce::Rectangle<int> Segmented::optionBounds (int index) const
{
    const auto font = mono (10.0f, Weight::Bold);
    const auto ideal = getIdealWidth();
    int x = 0;
    for (int i = 0; i < options.size(); ++i)
    {
        const auto natural = (int) juce::GlyphArrangement::getStringWidth (font, options[i].toUpperCase()) + 14;
        const auto w = ideal <= getWidth() ? natural : getWidth() / std::max (1, options.size());
        if (i == index)
            return { x, 0, w, getHeight() };
        x += w;
    }
    return {};
}

void Segmented::paint (juce::Graphics& g)
{
    if (options.isEmpty())
        return;
    const auto k = canAnimate (*this) ? easeOut ((float) ((juce::Time::getMillisecondCounterHiRes() - slideStart) / kSlide)) : 1.0f;
    const auto a = optionBounds (from).toFloat(), b = optionBounds (selected).toFloat();
    const juce::Rectangle<float> block (a.getX() + (b.getX() - a.getX()) * k, 0.0f, a.getWidth() + (b.getWidth() - a.getWidth()) * k,
                                        (float) getHeight());
    g.setColour (ink);
    g.fillRect (block);
    const auto mouse = isMouseOver() ? getMouseXYRelative() : juce::Point<int> (-1, -1);
    for (int i = 0; i < options.size(); ++i)
    {
        const auto ob = optionBounds (i);
        const auto inBlock = block.contains (ob.toFloat().getCentre());
        g.setColour (inBlock ? bg : (ob.contains (mouse) ? ink : muted));
        g.setFont (inBlock ? mono (10.0f, Weight::Bold) : mono (10.0f));
        g.drawText (options[i].toUpperCase(), ob, juce::Justification::centred, false);
        if (i > 0)
        {
            g.setColour (line);
            g.fillRect (ob.getX(), 0, 1, getHeight());
        }
    }
    g.setColour (line);
    g.drawRect (juce::Rectangle<int> (0, 0, optionBounds (options.size() - 1).getRight(), getHeight()), 1);
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < options.size(); ++i)
        if (optionBounds (i).contains (e.getPosition()))
        {
            setSelected (i, true);
            return;
        }
}

// TextLink -----------------------------------------------------------------------

TextLink::TextLink (const juce::String& text, bool b) : juce::Button (text), boxed (b) {}

void TextLink::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto on = getToggleState() || down;
    const auto r = getLocalBounds();
    if (on)
    {
        g.setColour (ink);
        g.fillRect (r);
    }
    else if (boxed)
    {
        g.setColour (over && isEnabled() ? ink : line);
        g.drawRect (r, 1);
    }
    g.setColour (! isEnabled() ? faint : on ? bg : (over || boxed ? ink : muted));
    g.setFont (on ? mono (10.0f, Weight::Bold) : mono (10.0f));
    g.drawText (getButtonText(), r, juce::Justification::centred, false);
}

// SolidButton --------------------------------------------------------------------

SolidButton::SolidButton (const juce::String& text, juce::Colour f) : juce::Button (text), fill (f) {}

void SolidButton::paintButton (juce::Graphics& g, bool, bool down)
{
    auto r = getLocalBounds();
    g.setColour (! isEnabled() ? faint : down ? ink : fill);
    g.fillRect (r);
    g.setColour (bg);
    g.setFont (mono (10.5f, Weight::Bold));
    if (withDice)
    {
        auto content = r.withSizeKeepingCentre ((int) juce::GlyphArrangement::getStringWidth (mono (10.5f, Weight::Bold), getButtonText()) + 28,
                                                r.getHeight());
        auto dice = content.removeFromLeft (13).withSizeKeepingCentre (12, 12).toFloat();
        g.drawRoundedRectangle (dice.reduced (0.5f), 2.0f, 1.2f);
        for (const auto& p : { dice.getCentre(), dice.getTopLeft() + juce::Point<float> (3.5f, 3.5f), dice.getBottomRight() - juce::Point<float> (3.5f, 3.5f) })
            g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre (p));
        content.removeFromLeft (7);
        g.drawText (getButtonText(), content, juce::Justification::centredLeft, false);
        return;
    }
    g.drawText (getButtonText(), r, juce::Justification::centred, false);
}

// Panels -------------------------------------------------------------------------

void drawPanel (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& tag)
{
    g.setColour (line);
    g.drawRect (bounds, 1);
    if (tag.isNotEmpty())
        drawTag (g, { bounds.getX() + 8, bounds.getY() + 7, 300, kTagHeight }, tag);
}

juce::Rectangle<int> panelContent (juce::Rectangle<int> bounds)
{
    return bounds.reduced (8, 7).withTrimmedTop (kTagHeight + 6);
}
