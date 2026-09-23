#include "BrowseStrip.h"

void ArrowTextButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), over, down);
    const auto r = getLocalBounds().toFloat().withSizeKeepingCentre (7.0f, 9.0f);
    juce::Path p;
    if (left)
        p.addTriangle (r.getRight(), r.getY(), r.getRight(), r.getBottom(), r.getX(), r.getCentreY());
    else
        p.addTriangle (r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getCentreY());
    g.setColour (juce::Colours::white.withAlpha (isEnabled() ? 0.85f : 0.3f));
    g.fillPath (p);
}

BrowseStrip::BrowseStrip()
{
    previous.setTooltip ("Previous in the library (LIB page's list)");
    next.setTooltip ("Next in the library (LIB page's list)");
    previous.setConnectedEdges (juce::Button::ConnectedOnRight);
    name.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    next.setConnectedEdges (juce::Button::ConnectedOnLeft);
    previous.onClick = [this] { if (onPrevious) onPrevious(); };
    next.onClick = [this] { if (onNext) onNext(); };
    name.onClick = [this] { if (onMenu) onMenu(); };
    for (juce::Button* b : { (juce::Button*) &previous, (juce::Button*) &name, (juce::Button*) &next })
        addAndMakeVisible (*b);
}

void BrowseStrip::setName (const juce::String& text, const juce::String& tooltip)
{
    if (text == shown)
        return;
    shown = text;
    name.setButtonText (text + juce::String::fromUTF8 (" \xe2\x96\xbe")); // ▾
    name.setTooltip (tooltip);
}

void BrowseStrip::resized()
{
    auto r = getLocalBounds();
    previous.setBounds (r.removeFromLeft (22));
    next.setBounds (r.removeFromRight (22));
    name.setBounds (r);
}
