#include "SeqPage.h"

using namespace batida;

namespace
{
std::optional<Pattern> patternClipboard;

void styleLabel (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (12.0f));
    l.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    l.setJustificationType (juce::Justification::centredRight);
}

juce::RangedAudioParameter& paramOf (BatidaProcessor& p, int g)
{
    return *p.getState().getParameter (globalParamID (g));
}
} // namespace

SeqPage::SeqPage (BatidaProcessor& p)
    : proc (p), grid (p), pageMap (p, grid),
      tempo (paramOf (p, gp::SeqTempo), 2, "bpm"),
      swing (paramOf (p, gp::SeqSwing), 0, "%", 100.0)
{
    auto& state = p.getState();
    styleLabel (patternLabel, "Pattern");
    styleLabel (lengthLabel, "Length");
    styleLabel (runLabel, "Run");
    styleLabel (quantiseLabel, "Start on");
    styleLabel (tempoLabel, "Tempo");
    styleLabel (swingLabel, "Swing");
    styleLabel (laneLabel, "Edit");

    for (int i = 1; i <= kNumPatterns; ++i)
        patternBox.addItem (juce::String (i), i);
    runBox.addItemList ({ "Keys", "Transport" }, 1);
    quantiseBox.addItemList ({ "Step", "Beat", "Bar" }, 1);
    patternAttachment = std::make_unique<ComboAttachment> (state, globalParamID (gp::SeqPattern), patternBox);
    runAttachment = std::make_unique<ComboAttachment> (state, globalParamID (gp::SeqRun), runBox);
    quantiseAttachment = std::make_unique<ComboAttachment> (state, globalParamID (gp::SeqQuantise), quantiseBox);
    latchAttachment = std::make_unique<ButtonAttachment> (state, globalParamID (gp::SeqLatch), latchButton);
    playAttachment = std::make_unique<ButtonAttachment> (state, globalParamID (gp::SeqPlay), playButton);
    syncAttachment = std::make_unique<ButtonAttachment> (state, globalParamID (gp::SeqSync), syncButton);
    syncButton.setTooltip ("On: follow the project's tempo and position while Logic plays. "
                           "Off: always run on Batida's own tempo.");

    // While following the project, show its tempo instead of our own.
    tempo.overrideText = [this]() -> std::optional<juce::String>
    {
        const auto& kit = proc.getKit();
        if (kit.isFollowingHost())
            return juce::String (kit.getHostBpm(), 2) + " bpm (project)";
        return std::nullopt;
    };

    lengthSlider.setSliderStyle (juce::Slider::IncDecButtons);
    lengthSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 34, 22);
    lengthSlider.setRange (kMinPatternSteps, kMaxSteps, 1);
    lengthSlider.onValueChange = [this]
    {
        const auto pi = proc.displayPattern();
        const auto len = (int) lengthSlider.getValue();
        if (proc.patterns().get().patterns[(size_t) pi].length == len)
            return;
        proc.patterns().edit ([&] (PatternBank& b)
        {
            auto& pat = b.patterns[(size_t) pi];
            for (auto& t : pat.tracks) // tracks that ran the full pattern keep doing so
                if (t.length >= pat.length)
                    t.length = len;
            pat.length = len;
        });
    };

    copyButton.onClick = [this] { patternClipboard = proc.patterns().get().patterns[(size_t) proc.displayPattern()]; };
    pasteButton.onClick = [this]
    {
        if (patternClipboard)
            proc.patterns().edit ([&] (PatternBank& b) { b.patterns[(size_t) proc.displayPattern()] = *patternClipboard; });
    };
    clearButton.onClick = [this] { proc.patterns().edit ([&] (PatternBank& b) { b.patterns[(size_t) proc.displayPattern()].clear(); }); };

    static const char* laneNames[] = { "Steps", "Pitch", "Slice", "Ratchet", "Prob" };
    for (int i = 0; i < (int) laneButtons.size(); ++i)
    {
        auto& b = laneButtons[(size_t) i];
        b.setButtonText (laneNames[i]);
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2b7bb9));
        b.onClick = [this, i] { setLane ((PatternGrid::Lane) i); };
        addAndMakeVisible (b);
    }
    setLane (PatternGrid::Lane::Steps);

    grid.onPageChanged = [this] { pageMap.repaint(); };
    grid.setComponentID ("grid");
    tempo.setComponentID ("tempo");

    hint.setText ("Steps: tap = on/off, drag up/down = velocity (to the bottom = off), shift-drag = paint. "
                  "Option-click = track length. Hold C3-D#4 to play patterns 1-16.",
                  juce::dontSendNotification);
    hint.setFont (juce::FontOptions (12.0f));
    hint.setColour (juce::Label::textColourId, juce::Colours::grey);

    for (juce::Component* c : { (juce::Component*) &patternLabel, (juce::Component*) &patternBox, (juce::Component*) &lengthLabel,
                                (juce::Component*) &lengthSlider, (juce::Component*) &runLabel, (juce::Component*) &runBox,
                                (juce::Component*) &quantiseLabel, (juce::Component*) &quantiseBox, (juce::Component*) &latchButton,
                                (juce::Component*) &playButton, (juce::Component*) &syncButton, (juce::Component*) &tempoLabel,
                                (juce::Component*) &tempo, (juce::Component*) &swingLabel, (juce::Component*) &swing,
                                (juce::Component*) &copyButton, (juce::Component*) &pasteButton, (juce::Component*) &clearButton,
                                (juce::Component*) &laneLabel, (juce::Component*) &pageMap, (juce::Component*) &grid,
                                (juce::Component*) &hint })
        addAndMakeVisible (c);

    timerCallback();
    startTimerHz (10);
}

SeqPage::~SeqPage()
{
    stopTimer();
}

void SeqPage::setLane (PatternGrid::Lane lane)
{
    grid.setLane (lane);
    for (int i = 0; i < (int) laneButtons.size(); ++i)
        laneButtons[(size_t) i].setToggleState (i == (int) lane, juce::dontSendNotification);
}

void SeqPage::timerCallback()
{
    // The strip follows the pattern shown (a pattern key can switch it).
    lengthSlider.setValue (proc.patterns().get().patterns[(size_t) proc.displayPattern()].length, juce::dontSendNotification);
}

void SeqPage::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2d31));
}

void SeqPage::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto place = [] (juce::Rectangle<int>& row, juce::Component& c, int w, int gap = 6)
    {
        c.setBounds (row.removeFromLeft (w).reduced (0, 2));
        row.removeFromLeft (gap);
    };

    auto rowA = area.removeFromTop (32);
    place (rowA, patternLabel, 50, 2);
    place (rowA, patternBox, 58);
    place (rowA, lengthLabel, 46, 2);
    place (rowA, lengthSlider, 92);
    place (rowA, runLabel, 30, 2);
    place (rowA, runBox, 96);
    place (rowA, quantiseLabel, 56, 2);
    place (rowA, quantiseBox, 70);
    place (rowA, latchButton, 62);
    place (rowA, playButton, 56);
    place (rowA, syncButton, 58);
    place (rowA, tempoLabel, 44, 2);
    place (rowA, tempo, 170);

    area.removeFromTop (6);
    auto rowB = area.removeFromTop (30);
    place (rowB, laneLabel, 30, 4);
    for (auto& b : laneButtons)
        place (rowB, b, 64, 2);
    rowB.removeFromLeft (10);
    place (rowB, pageMap, 290, 10);
    place (rowB, swingLabel, 42, 2);
    place (rowB, swing, 62, 10);
    place (rowB, copyButton, 52, 2);
    place (rowB, pasteButton, 52, 2);
    place (rowB, clearButton, 52, 2);

    hint.setBounds (area.removeFromBottom (20));
    area.removeFromTop (8);
    grid.setBounds (area);
}
