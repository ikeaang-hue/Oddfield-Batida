#include "FmPage.h"

#include "Engine/FmSource.h"

using namespace batida;

// AlgorithmDiagram --------------------------------------------------------------

void AlgorithmDiagram::setAlgorithm (int index)
{
    if (index != algorithm)
    {
        algorithm = index;
        repaint();
    }
}

void AlgorithmDiagram::paint (juce::Graphics& g)
{
    const auto& alg = algorithms()[(size_t) algorithm];
    const auto area = getLocalBounds().toFloat().reduced (6.0f);

    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    // Row 0 = carriers; a modulator sits one row above the highest op it feeds.
    std::array<int, kNumOps> row {};
    for (int k = 0; k < kNumOps; ++k)
    {
        if (alg.isCarrier (k))
            continue;
        for (int j = 0; j < k; ++j)
            if ((alg.modulators[(size_t) j] >> k) & 1)
                row[(size_t) k] = std::max (row[(size_t) k], row[(size_t) j] + 1);
    }

    const auto rows = *std::max_element (row.begin(), row.end()) + 1;
    const float boxW = 34.0f, boxH = 22.0f;
    const auto outY = area.getBottom() - 8.0f;
    const auto rowStep = std::min (40.0f, (outY - area.getY() - boxH - 10.0f) / (float) std::max (1, rows - 1));

    std::array<juce::Rectangle<float>, kNumOps> box;
    for (int r = 0; r < rows; ++r)
    {
        std::vector<int> inRow;
        for (int k = 0; k < kNumOps; ++k)
            if (row[(size_t) k] == r)
                inRow.push_back (k);

        const auto y = outY - 14.0f - boxH - (float) r * rowStep;
        for (size_t i = 0; i < inRow.size(); ++i)
        {
            const auto cx = area.getX() + area.getWidth() * ((float) i + 0.5f) / (float) inRow.size();
            box[(size_t) inRow[i]] = { cx - boxW / 2.0f, y, boxW, boxH };
        }
    }

    g.setColour (juce::Colours::lightgrey);
    for (int k = 0; k < kNumOps; ++k)
    {
        for (int j = 0; j < k; ++j)
            if ((alg.modulators[(size_t) j] >> k) & 1)
                g.drawArrow ({ box[(size_t) k].getCentreX(), box[(size_t) k].getBottom(),
                               box[(size_t) j].getCentreX(), box[(size_t) j].getY() },
                             1.5f, 6.0f, 6.0f);

        if (alg.isCarrier (k))
            g.drawLine (box[(size_t) k].getCentreX(), box[(size_t) k].getBottom(), box[(size_t) k].getCentreX(), outY, 1.5f);
    }

    // Output bus
    std::vector<float> carrierX;
    for (int k = 0; k < kNumOps; ++k)
        if (alg.isCarrier (k))
            carrierX.push_back (box[(size_t) k].getCentreX());
    const auto [minX, maxX] = std::minmax_element (carrierX.begin(), carrierX.end());
    g.drawLine (*minX - 4.0f, outY, *maxX + 4.0f, outY, 2.0f);

    // Feedback loop on op 4
    const auto fb = box[3];
    juce::Path loop;
    loop.startNewSubPath (fb.getRight(), fb.getCentreY() + 4.0f);
    loop.lineTo (fb.getRight() + 8.0f, fb.getCentreY() + 4.0f);
    loop.lineTo (fb.getRight() + 8.0f, fb.getY() - 5.0f);
    loop.lineTo (fb.getCentreX(), fb.getY() - 5.0f);
    loop.lineTo (fb.getCentreX(), fb.getY());
    g.strokePath (loop, juce::PathStrokeType (1.2f));

    for (int k = 0; k < kNumOps; ++k)
    {
        g.setColour (alg.isCarrier (k) ? juce::Colour (0xff4fc3f7) : juce::Colour (0xff9575cd));
        g.fillRoundedRectangle (box[(size_t) k], 3.0f);
        g.setColour (juce::Colours::black);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (juce::String (k + 1), box[(size_t) k], juce::Justification::centred);
    }
}

// FmPage ------------------------------------------------------------------------

FmPage::FmPage (BatidaProcessor& p) : VoicePage (p)
{
    fmSection = addSection ("FM");
    add (fmSection, vp::FmPitch, "Pitch");
    add (fmSection, vp::FmAlgo).withWidth (84);
    feedback = &add (fmSection, vp::FmFeedback);

    macroSection = addSection ("Macros");
    add (macroSection, vp::FmHarm);
    add (macroSection, vp::FmBright);
    add (macroSection, vp::FmBrightDecay);
    add (macroSection, vp::FmGrit);

    for (int o = 0; o < kNumOps; ++o)
    {
        opSections[(size_t) o] = addSection ("Op " + juce::String (o + 1));
        auto& s = opSections[(size_t) o];
        ops[(size_t) o].ratio = &add (s, opParam (o, Ratio), "Ratio");
        add (s, opParam (o, Fixed), "Fixed Freq");
        ops[(size_t) o].freq = &add (s, opParam (o, Freq), "Freq");
        ops[(size_t) o].level = &add (s, opParam (o, OpLevel), "Level");
        add (s, opParam (o, Wave), "Wave");
        add (s, opParam (o, Fold), "Fold");
        add (s, opParam (o, OpA), "Attack");
        ops[(size_t) o].decay = &add (s, opParam (o, OpD), "Decay");
        add (s, opParam (o, OpS), "Sustain");
        ops[(size_t) o].release = &add (s, opParam (o, OpR), "Release");
    }

    addAndMakeVisible (diagram);
    algoLabel.setFont (juce::FontOptions (12.0f));
    algoLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    algoLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (algoLabel);

    startTimerHz (20);
}

void FmPage::bindVoice (int newVoice)
{
    VoicePage::bindVoice (newVoice);
    lastAlgorithm = -1;
    timerCallback();
}

void FmPage::timerCallback()
{
    const auto p = proc.readVoiceParams (voice);
    const auto fm = computeEffectiveFm (p);

    if (fm.algorithm != lastAlgorithm)
    {
        lastAlgorithm = fm.algorithm;
        diagram.setAlgorithm (fm.algorithm);
        algoLabel.setText (juce::String ("Algorithm ") + juce::String (fm.algorithm + 1) + ":  "
                               + algorithms()[(size_t) fm.algorithm].description,
                           juce::dontSendNotification);
        for (int o = 0; o < kNumOps; ++o)
            setSectionTitle (opSections[(size_t) o], "Op " + juce::String (o + 1)
                                                         + (fm.ops[(size_t) o].carrier ? "  carrier" : "  modulator"));
        repaint();
    }

    auto marker = [] (float effective, float base) -> std::optional<double>
    {
        if (std::abs (effective - base) <= 1.0e-4f * std::max (1.0f, std::abs (base)))
            return std::nullopt;
        return (double) effective;
    };

    feedback->setMarker (marker (fm.feedback, p[vp::FmFeedback]));
    for (int o = 0; o < kNumOps; ++o)
    {
        const auto& e = fm.ops[(size_t) o];
        ops[(size_t) o].ratio->setMarker (marker (e.ratio, p.op (o, Ratio)));
        ops[(size_t) o].ratio->setAlpha (e.fixed ? 0.35f : 1.0f);
        ops[(size_t) o].freq->setAlpha (e.fixed ? 1.0f : 0.35f);
        ops[(size_t) o].level->setMarker (marker (e.level, p.op (o, OpLevel)));
        ops[(size_t) o].decay->setMarker (marker (e.decay, p.op (o, OpD)));
        ops[(size_t) o].release->setMarker (marker (e.release, p.op (o, OpR)));
    }
}

void FmPage::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto left = area.removeFromLeft (2 * ParamControl::kWidth + 84 + 12);
    area.removeFromLeft (10);

    left.removeFromTop (layoutRow (left, { fmSection }) + 8);
    algoLabel.setBounds (left.removeFromTop (16));
    diagram.setBounds (left.removeFromTop (130));
    left.removeFromTop (8);
    layoutRow (left, { macroSection }, 2);

    layoutRow (area, { opSections[0], opSections[1], opSections[2], opSections[3] }, 2);
}
