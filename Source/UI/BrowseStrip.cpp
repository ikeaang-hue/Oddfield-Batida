#include "BrowseStrip.h"

using namespace theme;

namespace
{
constexpr int kArrow = 18;
constexpr int kReload = 16;

// ↻: an open circle with an arrowhead, drawn (so it doesn't depend on the font).
void drawReload (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour)
{
    const auto c = r.getCentre();
    const auto radius = 4.5f;
    constexpr auto pi = juce::MathConstants<float>::pi;
    const auto start = 0.35f * pi, end = start + 1.6f * pi;
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, start, end, true);
    g.setColour (colour);
    g.strokePath (arc, juce::PathStrokeType (1.4f));
    // The head at the arc's end, pointing along it (clockwise).
    const juce::Point<float> tip { c.x + radius * std::sin (end), c.y - radius * std::cos (end) };
    const auto along = juce::Point<float> { std::cos (end), std::sin (end) };
    const auto across = juce::Point<float> { -along.y, along.x };
    juce::Path head;
    head.addTriangle (tip + along * 2.6f, tip - across * 2.4f - along * 0.6f, tip + across * 2.4f - along * 0.6f);
    g.fillPath (head);
}
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
    tip = tooltip;
    setTooltip (changed ? tip + " (changed on disk)" : tip);
}

void BrowseStrip::setChanged (bool c)
{
    if (c == changed)
        return;
    changed = c;
    setTooltip (changed ? tip + " (changed on disk)" : tip);
    repaint();
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
    if (changed && p.x >= getWidth() - kArrow - kReload)
        return Part::Reload;
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
    if (changed)
    {
        auto icon = r.removeFromRight (kReload).toFloat();
        if (style == Style::Large)
            icon = icon.withHeight ((float) r.getHeight() * 11.0f / 20.0f).withTrimmedTop (3.0f);
        drawReload (g, icon, hot == Part::Reload ? lime : lime.withAlpha (0.8f));
    }

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
        case Part::Reload:   if (onReload) onReload(); break;
        case Part::None:     break;
    }
}
