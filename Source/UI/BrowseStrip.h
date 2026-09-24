#pragma once

#include "Look/Theme.h"

// ‹ name › : steps through the library (the LIB list for that kind, with its
// filters); the name opens that level's menu (Init, Load, Save...).
// Compact: one line ("kit: Default Kit"). Large: a name in the heading face
// with a line under it ("slot 1 · C1 · Kick (Factory)").
class BrowseStrip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Style { Compact, Large };
    explicit BrowseStrip (Style style = Style::Compact);

    std::function<void()> onPrevious, onNext, onMenu;
    void setText (const juce::String& name, const juce::String& tooltip = {});
    void setPrefix (const juce::String& prefix) { prefixText = prefix; repaint(); } // "kit: "
    void setSubtitle (const juce::String& subtitle);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseMove (const juce::MouseEvent&) override { repaint(); }

private:
    enum class Part { None, Previous, Name, Next };
    Part partAt (juce::Point<int> p) const;

    const Style style;
    juce::String shown, prefixText, subtitle;
};
