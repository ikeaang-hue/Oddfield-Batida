#include "BrowseStrip.h"

using namespace theme;

namespace
{
constexpr int kArrow = 18;
} // namespace

BrowseStrip::BrowseStrip (Style s) : style (s)
{
    setRepaintsOnMouseActivity (false);
}

void BrowseStrip::setText (const juce::String& text, const juce::String& tooltip)
{
    if (text != shown)
    {
        shown = text;
        repaint();
    }
    setTooltip (tooltip);
}

void BrowseStrip::setSubtitle (const juce::String& text)
{
    if (text != subtitle)
    {
        subtitle = text;
        repaint();
    }
}

BrowseStrip::Part BrowseStrip::partAt (juce::Point<int> p) const
{
    if (! getLocalBounds().contains (p))
        return Part::None;
    if (p.x < kArrow)
        return Part::Previous;
    if (p.x >= getWidth() - kArrow)
        return Part::Next;
    return Part::Name;
}

void BrowseStrip::paint (juce::Graphics& g)
{
    const auto hot = isMouseOver() ? partAt (getMouseXYRelative()) : Part::None;
    auto r = getLocalBounds();
    const auto arrowFont = mono (style == Style::Large ? 14.0f : 12.0f);

    g.setFont (arrowFont);
    g.setColour (hot == Part::Previous ? ink : muted);
    g.drawText (juce::String::fromUTF8 ("\xe2\x80\xb9"), r.removeFromLeft (kArrow), juce::Justification::centred);
    g.setColour (hot == Part::Next ? ink : muted);
    g.drawText (juce::String::fromUTF8 ("\xe2\x80\xba"), r.removeFromRight (kArrow), juce::Justification::centred);

    if (style == Style::Large)
    {
        auto top = r.removeFromTop (r.getHeight() * 11 / 20).withTrimmedLeft (4);
        g.setColour (ink);
        g.setFont (head (14.0f, true));
        g.drawText (shown.toUpperCase(), top, juce::Justification::bottomLeft, true);
        g.setColour (muted);
        g.setFont (mono (9.5f));
        g.drawText (subtitle, r.withTrimmedLeft (4).withTrimmedTop (3), juce::Justification::topLeft, true);
        if (hot == Part::Name)
        {
            g.setColour (faint);
            g.fillRect (top.getX(), top.getBottom() + 1, (int) juce::GlyphArrangement::getStringWidth (head (14.0f, true), shown.toUpperCase()), 1);
        }
        return;
    }

    g.setFont (mono (11.0f, Weight::Medium));
    const auto text = prefixText + shown;
    g.setColour (hot == Part::Name ? ink : ink.withAlpha (0.9f));
    g.drawText (text, r, juce::Justification::centred, true);
    if (hot == Part::Name)
    {
        const auto w = std::min (r.getWidth(), (int) juce::GlyphArrangement::getStringWidth (mono (11.0f, Weight::Medium), text));
        g.setColour (faint);
        g.fillRect (r.getCentreX() - w / 2, r.getCentreY() + 8, w, 1);
    }
}

void BrowseStrip::mouseDown (const juce::MouseEvent& e)
{
    switch (partAt (e.getPosition()))
    {
        case Part::Previous: if (onPrevious) onPrevious(); break;
        case Part::Next:     if (onNext) onNext(); break;
        case Part::Name:     if (onMenu) onMenu(); break;
        case Part::None:     break;
    }
}
