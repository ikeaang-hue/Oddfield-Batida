#pragma once

#include "VoicePages.h"

// Draws the operator routing of one algorithm: modulators above the ops they
// modulate, carriers on the bottom row feeding the output.
class AlgorithmDiagram final : public juce::Component
{
public:
    void setAlgorithm (int index);
    void paint (juce::Graphics& g) override;

private:
    int algorithm = 0;
};

// FM source: pitch, algorithm, feedback, the four macros and the four
// operators. Orange dots on operator knobs show the effective value after the
// macros, so turning a macro visibly moves the real parameters.
class FmPage final : public VoicePage, private juce::Timer
{
public:
    explicit FmPage (BatidaProcessor& p);

    void bindVoice (int newVoice) override;
    void resized() override;

private:
    void timerCallback() override;

    int fmSection, macroSection;
    std::array<int, batida::kNumOps> opSections {};
    AlgorithmDiagram diagram;
    juce::Label algoLabel;

    ParamControl* feedback = nullptr;
    struct OpControls
    {
        ParamControl *ratio, *freq, *level, *decay, *release;
    };
    std::array<OpControls, batida::kNumOps> ops {};
    int lastAlgorithm = -1;
};
