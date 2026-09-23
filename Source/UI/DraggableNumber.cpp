#include "DraggableNumber.h"

namespace
{
const juce::FontOptions kFont (17.0f, juce::Font::bold);
constexpr float kPixelsPerStep = 5.0f;
} // namespace

DraggableNumber::DraggableNumber (juce::RangedAudioParameter& p, int d, juce::String s, double displayScale)
    : param (p), decimals (d), suffix (std::move (s)), scale (displayScale)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setTooltip ("Drag up/down" + juce::String (d > 0 ? " (whole part or decimals separately)" : "")
                + "; double-click to type");
    startTimerHz (15);
}

double DraggableNumber::value() const
{
    return param.convertFrom0to1 (param.getValue()) * scale;
}

void DraggableNumber::setValue (double v)
{
    const auto range = param.getNormalisableRange();
    const auto raw = juce::jlimit ((double) range.start, (double) range.end, v / scale);
    param.setValueNotifyingHost (param.convertTo0to1 ((float) raw));
}

juce::String DraggableNumber::integerText() const
{
    const auto v = value();
    if (decimals == 0)
        return juce::String (juce::roundToInt (v));
    return juce::String ((int) std::floor (v + 0.5 * std::pow (10.0, -decimals)));
}

juce::String DraggableNumber::fractionText() const
{
    if (decimals == 0)
        return {};
    const auto v = value() + 0.5 * std::pow (10.0, -decimals);
    const auto frac = (int) std::floor ((v - std::floor (v)) * std::pow (10.0, decimals));
    return "." + juce::String (frac).paddedLeft ('0', decimals);
}

juce::Rectangle<int> DraggableNumber::textArea() const
{
    return getLocalBounds().reduced (6, 0);
}

int DraggableNumber::splitX() const
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (juce::Font (kFont), integerText(), 0.0f, 0.0f);
    return textArea().getX() + juce::roundToInt (ga.getBoundingBox (0, -1, true).getRight());
}

void DraggableNumber::timerCallback()
{
    const auto o = overrideText ? overrideText() : std::nullopt;
    if (param.getValue() != shown || o != shownOverride)
        repaint();
}

void DraggableNumber::paint (juce::Graphics& g)
{
    shown = param.getValue();
    shownOverride = overrideText ? overrideText() : std::nullopt;

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    const auto area = textArea();
    if (shownOverride.has_value())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (*shownOverride, area, juce::Justification::centredLeft);
        return;
    }

    const auto split = splitX();
    auto highlight = [&] (bool fractionPart)
    {
        return dragging && dragFraction == fractionPart ? juce::Colours::orange : juce::Colours::white;
    };

    g.setFont (kFont);
    g.setColour (highlight (false));
    g.drawText (integerText(), area, juce::Justification::centredLeft);
    g.setColour (highlight (true));
    g.drawText (fractionText(), area.withLeft (split), juce::Justification::centredLeft);

    juce::GlyphArrangement ga;
    ga.addLineOfText (juce::Font (kFont), fractionText(), 0.0f, 0.0f);
    const auto end = split + juce::roundToInt (ga.getBoundingBox (0, -1, true).getRight());
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (suffix, area.withLeft (end + 4), juce::Justification::centredLeft);
}

void DraggableNumber::mouseDown (const juce::MouseEvent& e)
{
    if (overrideText && overrideText().has_value())
        return; // following the host: nothing to drag
    dragging = true;
    dragFraction = decimals > 0 && e.x >= splitX();
    startValue = value();
    param.beginChangeGesture();
    repaint();
}

void DraggableNumber::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    const auto steps = std::round (-e.getDistanceFromDragStartY() / kPixelsPerStep);
    const auto unit = dragFraction ? std::pow (10.0, -decimals) : 1.0;
    setValue (startValue + steps * unit);
    repaint();
}

void DraggableNumber::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        param.endChangeGesture();
    dragging = false;
    repaint();
}

void DraggableNumber::mouseDoubleClick (const juce::MouseEvent&)
{
    if (overrideText && overrideText().has_value())
        return;
    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (juce::FontOptions (16.0f));
    editor->setText (juce::String (value(), decimals), false);
    editor->setBounds (getLocalBounds());
    editor->selectAll();
    auto commit = [this]
    {
        if (editor == nullptr)
            return;
        const auto text = editor->getText();
        param.beginChangeGesture();
        setValue (text.getDoubleValue());
        param.endChangeGesture();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DraggableNumber> (this)]
        {
            if (safe != nullptr)
                safe->editor.reset();
        });
    };
    editor->onReturnKey = commit;
    editor->onFocusLost = commit;
    editor->onEscapeKey = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DraggableNumber> (this)]
        {
            if (safe != nullptr)
                safe->editor.reset();
        });
    };
    addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();
}
