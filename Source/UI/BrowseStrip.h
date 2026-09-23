#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// A button with a drawn triangle (text arrows don't fit narrow buttons).
struct ArrowTextButton final : juce::TextButton
{
    explicit ArrowTextButton (bool pointsLeft) : left (pointsLeft) {}
    void paintButton (juce::Graphics& g, bool over, bool down) override;
    const bool left;
};

// ◀ name ▾ ▶ : steps through the library (the LIB page's list for that kind,
// with its filters) and opens the menu for that level (Init, Load, Save...).
class BrowseStrip final : public juce::Component
{
public:
    BrowseStrip();

    std::function<void()> onPrevious, onNext, onMenu;
    void setName (const juce::String& name, const juce::String& tooltip = {});
    void resized() override;

private:
    ArrowTextButton previous { true }, next { false };
    juce::TextButton name;
    juce::String shown;
};
