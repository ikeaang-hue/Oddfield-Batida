#include "ParamControl.h"

void MarkedSlider::setMarker (std::optional<double> value)
{
    if (value != marker)
    {
        marker = value;
        repaint();
    }
}

void MarkedSlider::paintOverChildren (juce::Graphics& g)
{
    if (! marker.has_value())
        return;

    // Matches LookAndFeel_V4::drawRotarySlider's geometry.
    const auto layout = getLookAndFeel().getSliderLayout (*this);
    const auto bounds = layout.sliderBounds.toFloat().reduced (10.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto lineW = juce::jmin (8.0f, radius * 0.5f);
    const auto arcRadius = radius - lineW * 0.5f;
    const auto rp = getRotaryParameters();

    const auto prop = (float) juce::jlimit (0.0, 1.0, valueToProportionOfLength (*marker));
    const auto angle = rp.startAngleRadians + prop * (rp.endAngleRadians - rp.startAngleRadians);
    const auto centre = bounds.getCentre();
    const juce::Point<float> p (centre.x + arcRadius * std::sin (angle), centre.y - arcRadius * std::cos (angle));

    g.setColour (juce::Colours::orange);
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p));
    g.setColour (juce::Colours::black);
    g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p), 1.0f);
}

ParamControl::ParamControl (juce::AudioProcessorValueTreeState& s, juce::String labelText)
    : state (s), fixedLabel (std::move (labelText))
{
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (12.0f));
    label.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (label);

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, kWidth - 4, 16);
    addChildComponent (slider);
    addChildComponent (combo);
    addChildComponent (toggle);
}

void ParamControl::bind (const juce::String& paramID)
{
    sliderAttachment.reset();
    comboAttachment.reset();
    toggleAttachment.reset();

    auto* param = state.getParameter (paramID);
    jassert (param != nullptr);
    if (param == nullptr)
        return;

    // "V3 Op2 Ratio" → "Op2 Ratio": the voice is shown by the selector.
    auto name = param->getName (64);
    if (name.startsWithChar ('V') && name.containsChar (' ') && juce::CharacterFunctions::isDigit (name[1]))
        name = name.fromFirstOccurrenceOf (" ", false, false);
    label.setText (fixedLabel.isNotEmpty() ? fixedLabel : name, juce::dontSendNotification);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
    {
        type = Type::Choice;
        combo.clear (juce::dontSendNotification);
        combo.addItemList (choice->choices, 1);
        comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramID, combo);
    }
    else if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
    {
        type = Type::Toggle;
        toggle.setButtonText ("On");
        toggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramID, toggle);
    }
    else
    {
        type = Type::Knob;
        sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramID, slider);
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    }

    slider.setVisible (type == Type::Knob);
    combo.setVisible (type == Type::Choice);
    toggle.setVisible (type == Type::Toggle);
    resized();
}

void ParamControl::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));

    switch (type)
    {
        case Type::Knob:   slider.setBounds (r); break;
        case Type::Choice: combo.setBounds (r.withSizeKeepingCentre (r.getWidth() - 4, 24)); break;
        case Type::Toggle: toggle.setBounds (r.withSizeKeepingCentre (56, 24)); break;
        case Type::None:   break;
    }
}
