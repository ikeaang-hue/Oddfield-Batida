#include "ParamControl.h"

#include "Plugin/PluginProcessor.h"

namespace
{
BatidaProcessor* processorOf (juce::AudioProcessorValueTreeState& state)
{
    return dynamic_cast<BatidaProcessor*> (&state.processor);
}
} // namespace

void MarkedSlider::setMarker (std::optional<double> value)
{
    if (value != marker)
    {
        marker = value;
        repaint();
    }
}

void MarkedSlider::setModulation (std::optional<Modulation> m)
{
    if (m.has_value() != modulation.has_value() || (m && *m != *modulation))
    {
        modulation = m;
        repaint();
    }
}

void MarkedSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick)
    {
        onRightClick();
        return;
    }
    juce::Slider::mouseDown (e);
}

void MarkedSlider::paintOverChildren (juce::Graphics& g)
{
    if (! marker.has_value() && ! modulation.has_value())
        return;

    // Matches LookAndFeel_V4::drawRotarySlider's geometry.
    const auto layout = getLookAndFeel().getSliderLayout (*this);
    const auto bounds = layout.sliderBounds.toFloat().reduced (10.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto lineW = juce::jmin (8.0f, radius * 0.5f);
    const auto arcRadius = radius - lineW * 0.5f;
    const auto rp = getRotaryParameters();
    const auto centre = bounds.getCentre();
    auto angleOf = [&] (double v)
    {
        const auto prop = (float) juce::jlimit (0.0, 1.0, valueToProportionOfLength (v));
        return rp.startAngleRadians + prop * (rp.endAngleRadians - rp.startAngleRadians);
    };
    auto pointAt = [&] (float angle, float r)
    {
        return juce::Point<float> (centre.x + r * std::sin (angle), centre.y - r * std::cos (angle));
    };

    if (modulation.has_value())
    {
        const auto green = juce::Colour (0xff6fd08c);
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcRadius + lineW, arcRadius + lineW, 0.0f,
                           angleOf (modulation->low), angleOf (modulation->high), true);
        g.setColour (green.withAlpha (0.8f));
        g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (green);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (pointAt (angleOf (modulation->live), arcRadius + lineW)));
    }

    if (marker.has_value())
    {
        const auto p = pointAt (angleOf (*marker), arcRadius);
        g.setColour (juce::Colours::orange);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p));
        g.setColour (juce::Colours::black);
        g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p), 1.0f);
    }
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

    slider.onRightClick = [this] { showModulationMenu(); };
}

ParamControl::~ParamControl()
{
    stopTimer();
}

void ParamControl::timerCallback()
{
    if (auto* proc = processorOf (state))
    {
        if (const auto m = proc->modDisplayFor (boundID))
            slider.setModulation (MarkedSlider::Modulation { m->live, m->low, m->high });
        else
            slider.setModulation (std::nullopt);
    }
}

void ParamControl::showModulationMenu()
{
    auto* proc = processorOf (state);
    if (proc == nullptr || ! proc->isModulatable (boundID))
        return;

    juce::PopupMenu m;
    m.addSectionHeader (label.getText());
    for (int mod = 0; mod < batida::kNumMods; ++mod)
        m.addItem (mod + 1, "Modulate with Mod " + juce::String (mod + 1), true, proc->isModTarget (mod, boundID));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slider),
                     [proc, id = boundID, safe = juce::Component::SafePointer<ParamControl> (this)] (int choice)
                     {
                         if (choice > 0 && safe != nullptr)
                             proc->toggleModTarget (choice - 1, id);
                     });
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
    boundID = paramID;

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

    // Knobs that can be modulated watch for it (15 Hz).
    auto* proc = processorOf (state);
    if (type == Type::Knob && proc != nullptr && proc->isModulatable (paramID))
    {
        timerCallback();
        startTimerHz (15);
    }
    else
    {
        stopTimer();
        slider.setModulation (std::nullopt);
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
