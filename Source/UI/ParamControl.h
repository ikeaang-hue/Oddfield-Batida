#pragma once

#include "Look/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

// One host parameter, drawn the Signal way:
//   float  -> a value bar: LABEL  [====|---]  number
//   choice -> segmented buttons (up to 5 options) or a drop-down menu
//   bool   -> a chip ([TEXT] off, lime when on)
//
// Bars drag left/right or up/down, always from the current value (never
// jumping); Shift = fine; clicking the number types a value; double-click (or
// Option-click) glides back to the default; right-click opens Modulate / Type /
// Default; the wheel nudges.
// A lime tick and segment show where Batida moves the value (setMarker: XY,
// macros); a dim lime band and tick show modulation. bind() re-attaches to
// another parameter (the selected sound changes).
class ParamControl final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit ParamControl (juce::AudioProcessorValueTreeState& state, juce::String labelText = {});
    ~ParamControl() override;

    void bind (const juce::String& paramID);
    const juce::String& getParamID() const { return boundID; }

    // The effective value, in the parameter's units (nullopt = none).
    void setMarker (std::optional<double> value);
    void setLabelText (const juce::String& text);

    // Layout of a bar row (and the label of segmented / menu rows).
    ParamControl& withLabelWidth (int w) { labelWidth = w; repaint(); return *this; }
    ParamControl& withValueWidth (int w) { valueWidth = w; repaint(); return *this; }
    ParamControl& asMenu() { forceMenu = true; if (boundID.isNotEmpty()) bind (boundID); return *this; }
    ParamControl& asSegmented() { forceSegmented = true; if (boundID.isNotEmpty()) bind (boundID); return *this; }
    ParamControl& numberOnly() { onlyNumber = true; if (boundID.isNotEmpty()) bind (boundID); return *this; }
    ParamControl& withOptionLabels (juce::StringArray labels) { optionLabels = std::move (labels); repaint(); return *this; }
    ParamControl& withWidth (int) { return *this; } // old layouts; widths come from bounds now

    enum class Kind { None, Bar, Number, Segmented, Menu, Chip };
    Kind getKind() const { return kind; }

    // The width a segmented/chip control wants at its natural size.
    int getIdealWidth() const;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void modifierKeysChanged (const juce::ModifierKeys&) override;

private:
    void timerCallback() override;
    void valueChanged (float);
    void showMenu();
    void showChoiceMenu();
    void editValue();
    void glideTo (float normalised);
    void animate();
    float norm() const;                 // current value, 0..1
    float normOf (double value) const;  // a value in units, 0..1
    juce::String textFor (float normalised) const;
    juce::Rectangle<int> trackArea() const;
    juce::Rectangle<int> valueArea() const;
    juce::Rectangle<int> controlArea() const; // segmented / menu / chip
    juce::StringArray options() const;
    int optionAt (int x) const;
    juce::Rectangle<int> optionBounds (int index) const;
    bool isModulatable() const;

    juce::AudioProcessorValueTreeState& state;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::String fixedLabel, label, boundID;
    juce::StringArray optionLabels;
    Kind kind = Kind::None;
    bool forceMenu = false, forceSegmented = false, onlyNumber = false;
    int labelWidth = 62, valueWidth = 64;

    std::optional<double> marker;
    struct Modulation { float live, low, high; };
    std::optional<Modulation> modulation;

    // Drag state
    bool dragging = false, fine = false, pressedOnValue = false, movedSincePress = false;
    int clickToken = 0;
    float dragStartNorm = 0.0f;
    juce::Point<float> dragOrigin;
    juce::Point<float> lastMouse;

    // Animation: glides, the segmented block's slide, the chip's wipe.
    std::unique_ptr<juce::VBlankAttachment> vblank;
    struct Glide { float from, to; double start; };
    std::optional<Glide> glide;
    float shownIndex = -1.0f, slideFrom = 0.0f; // segmented
    double slideStart = 0.0;
    float chipFill = 0.0f;
    double chipStart = 0.0;
    bool chipTarget = false;
    float defaultFlash = 0.0f;
    double flashStart = 0.0;

    std::unique_ptr<juce::TextEditor> editor;
};
