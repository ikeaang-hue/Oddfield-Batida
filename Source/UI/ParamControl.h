#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

// A rotary slider that can also show a second marker: the effective value
// after the FM macros are applied.
class MarkedSlider final : public juce::Slider
{
public:
    void setMarker (std::optional<double> value);

    // Modulation: the range the modulators can reach (green arc) and where it
    // is now (green dot). Values in the knob's units.
    struct Modulation { double live, low, high; bool operator!= (const Modulation& o) const { return live != o.live || low != o.low || high != o.high; } };
    void setModulation (std::optional<Modulation> m);

    std::function<void()> onRightClick;

    void paintOverChildren (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    std::optional<double> marker;
    std::optional<Modulation> modulation;
};

// Label + the stock widget for one host parameter: a knob for floats, a combo
// box for choices, a toggle for bools. bind() re-attaches it to another
// parameter of the same shape (used when the selected voice changes).
class ParamControl final : public juce::Component, private juce::Timer
{
public:
    explicit ParamControl (juce::AudioProcessorValueTreeState& state, juce::String labelText = {});

    void bind (const juce::String& paramID);
    ~ParamControl() override;
    void resized() override;

    void setMarker (std::optional<double> value) { slider.setMarker (value); }
    void setLabelText (const juce::String& text) { label.setText (text, juce::dontSendNotification); }

    // Menus with long entries get a wider cell.
    ParamControl& withWidth (int w) { preferredWidth = w; return *this; }
    int getPreferredWidth() const { return preferredWidth; }

    static constexpr int kWidth = 74;
    static constexpr int kHeight = 88;

private:
    enum class Type { None, Knob, Choice, Toggle };

    void timerCallback() override;
    void showModulationMenu();

    juce::AudioProcessorValueTreeState& state;
    juce::String fixedLabel, boundID;
    Type type = Type::None;
    int preferredWidth = kWidth;

    juce::Label label;
    MarkedSlider slider;
    juce::ComboBox combo;
    juce::ToggleButton toggle;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttachment;
};
