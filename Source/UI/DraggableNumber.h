#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// A number you drag up/down, always from its current value (never jumps to
// where you clicked). With decimals, the integer part and the part after the
// point drag separately: the integer in whole steps, the fraction in its last
// digit. Double-click to type a value. Writes a host parameter with gestures.
class DraggableNumber final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    DraggableNumber (juce::RangedAudioParameter& param, int decimals, juce::String suffix,
                     double displayScale = 1.0);

    // Shown instead of the value while it returns something (e.g. the host tempo).
    std::function<std::optional<juce::String>()> overrideText;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    double value() const;
    void setValue (double v);
    juce::String integerText() const;
    juce::String fractionText() const;
    juce::Rectangle<int> textArea() const;
    int splitX() const; // x where the fraction starts

    juce::RangedAudioParameter& param;
    const int decimals;
    const juce::String suffix;
    const double scale; // shown value = parameter value × scale (e.g. 100 for %)

    bool dragging = false, dragFraction = false;
    double startValue = 0.0;
    float shown = -1.0f;
    std::optional<juce::String> shownOverride;
    std::unique_ptr<juce::TextEditor> editor;
};
