#pragma once

#include "Plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Guided variations for the selected voice: Vary proposes up to 4 nearby
// sounds (steered by a direction, with sections locked); hold a suggestion to
// hear it in place, Keep commits it (one undo step), Back returns.
class VaryPage final : public juce::Component, private juce::Timer
{
public:
    explicit VaryPage (BatidaProcessor& p);
    ~VaryPage() override;

    void bindVoice (int voice);
    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    struct PressButton final : juce::TextButton
    {
        std::function<void()> onPress, onRelease;
        void mouseDown (const juce::MouseEvent& e) override { juce::TextButton::mouseDown (e); if (onPress) onPress(); }
        void mouseUp (const juce::MouseEvent& e) override { juce::TextButton::mouseUp (e); if (onRelease) onRelease(); }
    };

    void timerCallback() override;
    void refresh();
    juce::String describe (const batida::SoundFeatures& f) const;

    BatidaProcessor& proc;
    int voice = 0, shownVersion = -1;
    batida::SoundFeatures baseFeatures;

    juce::Slider amount;
    juce::Label amountLabel, directionLabel, lockLabel, status, help;
    std::array<juce::TextButton, 9> directionButtons;
    juce::ToggleButton lockSource { "Source" }, lockFx { "Voice FX" }, lockEnvelopes { "Envelopes" };
    juce::TextButton varyButton { "Vary" }, keepButton { "Keep" }, backButton { "Back" };
    std::array<PressButton, 4> suggestions;
    PressButton originalButton;
    int direction = 0;
};
