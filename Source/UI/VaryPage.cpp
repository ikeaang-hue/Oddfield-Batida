#include "VaryPage.h"

using namespace batida;

namespace
{
const char* kDirections[] = { "Any", "Brighter", "Darker", "Shorter", "Longer", "Tonal", "Noisy", "Cleaner", "Dirtier" };

void styleLabel (juce::Label& l, const juce::String& text, float size = 13.0f)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (size, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
}
} // namespace

VaryPage::VaryPage (BatidaProcessor& p) : proc (p)
{
    styleLabel (amountLabel, "Amount");
    styleLabel (directionLabel, "Direction");
    styleLabel (lockLabel, "Keep as is");
    amountLabel.setJustificationType (juce::Justification::centred);

    amount.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    amount.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    amount.setRange (0.05, 1.0, 0.01);
    amount.setValue (0.35, juce::dontSendNotification);
    amount.textFromValueFunction = [] (double v) { return v < 0.3 ? juce::String ("subtle") : v < 0.65 ? juce::String ("medium") : juce::String ("far"); };
    amount.setTooltip ("How far the suggestions stray from the current sound");
    amount.updateText();

    for (int i = 0; i < 9; ++i)
    {
        auto& b = directionButtons[(size_t) i];
        b.setButtonText (kDirections[i]);
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2b7bb9));
        b.onClick = [this, i]
        {
            direction = i;
            for (int k = 0; k < 9; ++k)
                directionButtons[(size_t) k].setToggleState (k == i, juce::dontSendNotification);
        };
        addAndMakeVisible (b);
    }
    directionButtons[0].setToggleState (true, juce::dontSendNotification);

    varyButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffc0632a));
    varyButton.onClick = [this]
    {
        proc.startVary (voice, (float) amount.getValue(), (VaryDirection) direction, lockSource.getToggleState(),
                        lockFx.getToggleState(), lockEnvelopes.getToggleState());
        refresh();
    };

    for (int i = 0; i < 4; ++i)
    {
        auto& b = suggestions[(size_t) i];
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2b7bb9));
        b.onPress = [this, i]
        {
            proc.previewCandidate (i);
            proc.audition (proc.varyVoice(), true);
            refresh();
        };
        b.onRelease = [this] { proc.audition (proc.varyVoice(), false); };
        addAndMakeVisible (b);
    }
    originalButton.setButtonText ("Original");
    originalButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2b7bb9));
    originalButton.onPress = [this]
    {
        proc.previewCandidate (-1);
        proc.audition (voice, true);
        refresh();
    };
    originalButton.onRelease = [this] { proc.audition (voice, false); };

    keepButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3c7d4f));
    keepButton.onClick = [this] { proc.keepCandidate(); refresh(); };
    backButton.onClick = [this] { proc.previewCandidate (-1); refresh(); };

    status.setFont (juce::FontOptions (13.0f));
    status.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    help.setText ("Vary suggests up to 4 sounds close to this one. Hold a suggestion to hear it (it's only a preview), "
                  "Keep the one you like, then Vary again from there. Directions steer the suggestions; "
                  "locked sections stay as they are. Suggestions are level-matched to the original (Keep adjusts "
                  "Level to match); silent and near-identical results are filtered out.",
                  juce::dontSendNotification);
    help.setFont (juce::FontOptions (12.0f));
    help.setColour (juce::Label::textColourId, juce::Colours::grey);
    help.setJustificationType (juce::Justification::topLeft);

    for (juce::Component* c : { (juce::Component*) &amount, (juce::Component*) &amountLabel, (juce::Component*) &directionLabel,
                                (juce::Component*) &lockLabel, (juce::Component*) &lockSource, (juce::Component*) &lockFx,
                                (juce::Component*) &lockEnvelopes, (juce::Component*) &varyButton, (juce::Component*) &originalButton,
                                (juce::Component*) &keepButton, (juce::Component*) &backButton, (juce::Component*) &status,
                                (juce::Component*) &help })
        addAndMakeVisible (c);

    refresh();
    startTimerHz (8);
}

VaryPage::~VaryPage()
{
    stopTimer();
}

void VaryPage::bindVoice (int v)
{
    if (v != voice && proc.previewedCandidate() >= 0)
        proc.previewCandidate (-1);
    voice = v;
    refresh();
}

juce::String VaryPage::describe (const SoundFeatures& f) const
{
    juce::StringArray words;
    if (f.brightnessHz > baseFeatures.brightnessHz * 1.1f) words.add ("brighter");
    else if (f.brightnessHz < baseFeatures.brightnessHz * 0.9f) words.add ("darker");
    if (f.lengthMs > baseFeatures.lengthMs * 1.1f) words.add ("longer");
    else if (f.lengthMs < baseFeatures.lengthMs * 0.9f) words.add ("shorter");
    // Suggestions are level-matched, so this only shows when Level ran out of room.
    if (f.loudnessDb > baseFeatures.loudnessDb + 1.5f) words.add ("louder");
    else if (f.loudnessDb < baseFeatures.loudnessDb - 1.0f) words.add ("softer");
    return words.isEmpty() ? juce::String ("different tone") : words.joinIntoString (", ");
}

void VaryPage::timerCallback()
{
    if (proc.getVaryVersion() != shownVersion)
        refresh();
}

void VaryPage::refresh()
{
    shownVersion = proc.getVaryVersion();
    const auto& cands = proc.varyCandidates();
    const bool mine = proc.varyVoice() == voice && ! cands.empty();

    if (mine)
        baseFeatures = measureVoice (proc.varyBase(), proc.sampleSlot (voice).getDisplayData());

    for (int i = 0; i < 4; ++i)
    {
        auto& b = suggestions[(size_t) i];
        const bool has = mine && i < (int) cands.size();
        b.setEnabled (has);
        b.setButtonText (has ? juce::String (i + 1) + ": " + describe (cands[(size_t) i].features) : juce::String ("-"));
        b.setToggleState (has && proc.previewedCandidate() == i, juce::dontSendNotification);
    }
    originalButton.setToggleState (mine && proc.previewedCandidate() < 0, juce::dontSendNotification);
    keepButton.setEnabled (mine && proc.previewedCandidate() >= 0);
    backButton.setEnabled (mine && proc.previewedCandidate() >= 0);
    varyButton.setEnabled (! proc.isVarying());

    juce::String text;
    if (proc.isVarying())
        text = "Finding variations...";
    else if (mine)
        text = juce::String ((int) cands.size()) + " suggestion" + (cands.size() == 1 ? "" : "s")
             + (proc.previewedCandidate() >= 0 ? ": previewing " + juce::String (proc.previewedCandidate() + 1) + ". Keep it, or Back."
                                              : ". Hold one to hear it.");
    else if (proc.varyVoice() == voice)
        text = "No usable variations this time: try again, or a larger Amount.";
    else
        text = "Press Vary for suggestions for voice " + juce::String (voice + 1) + " (" + proc.getVoiceName (voice) + ").";
    status.setText (text, juce::dontSendNotification);
}

void VaryPage::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2d31));
}

void VaryPage::resized()
{
    auto r = getLocalBounds().reduced (12);

    auto top = r.removeFromTop (110);
    auto knob = top.removeFromLeft (90);
    amountLabel.setBounds (knob.removeFromTop (18));
    amount.setBounds (knob);
    top.removeFromLeft (14);

    auto dirs = top.removeFromLeft (400);
    directionLabel.setBounds (dirs.removeFromTop (18));
    auto row1 = dirs.removeFromTop (32), row2 = dirs.removeFromTop (32);
    directionButtons[0].setBounds (row1.removeFromLeft (76).reduced (2));
    for (int i = 1; i < 9; i += 2)
    {
        directionButtons[(size_t) i].setBounds (row1.removeFromLeft (80).reduced (2));
        directionButtons[(size_t) i + 1].setBounds (row2.withTrimmedLeft (76 + (i - 1) / 2 * 80).withWidth (80).reduced (2));
        directionButtons[(size_t) i + 1].setBounds (directionButtons[(size_t) i + 1].getBounds().withHeight (directionButtons[(size_t) i].getHeight()));
    }
    top.removeFromLeft (14);

    auto locks = top.removeFromLeft (130);
    lockLabel.setBounds (locks.removeFromTop (18));
    lockSource.setBounds (locks.removeFromTop (24));
    lockFx.setBounds (locks.removeFromTop (24));
    lockEnvelopes.setBounds (locks.removeFromTop (24));
    top.removeFromLeft (14);
    varyButton.setBounds (top.removeFromLeft (140).withSizeKeepingCentre (140, 56));

    r.removeFromTop (12);
    auto cands = r.removeFromTop (64);
    originalButton.setBounds (cands.removeFromLeft (110).reduced (3));
    for (auto& b : suggestions)
        b.setBounds (cands.removeFromLeft ((r.getWidth() - 110) / 4).reduced (3));

    r.removeFromTop (8);
    auto actions = r.removeFromTop (32);
    keepButton.setBounds (actions.removeFromLeft (110).reduced (3, 0));
    backButton.setBounds (actions.removeFromLeft (110).reduced (3, 0));
    actions.removeFromLeft (10);
    status.setBounds (actions);

    r.removeFromTop (14);
    help.setBounds (r.removeFromTop (60));
}
