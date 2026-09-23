#include "PluginEditor.h"

using namespace batida;

namespace
{
bool isAudioFile (const juce::String& path)
{
    const auto ext = juce::File (path).getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac";
}

juce::String drumNoteName (int voice)
{
    return juce::MidiMessage::getMidiNoteName (kDrumMapFirstNote + voice, true, true, 3);
}
} // namespace

void BatidaEditor::VoiceButton::mouseDown (const juce::MouseEvent& e)
{
    juce::TextButton::mouseDown (e);
    if (onPress)
        onPress();
}

void BatidaEditor::VoiceButton::mouseUp (const juce::MouseEvent& e)
{
    juce::TextButton::mouseUp (e);
    if (onRelease)
        onRelease();
}

BatidaEditor::BatidaEditor (BatidaProcessor& p)
    : AudioProcessorEditor (&p), proc (p), midiMode (p.getState()), master (p.getState())
{
    title.setText ("BATIDA", juce::dontSendNotification);
    title.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    addAndMakeVisible (title);

    info.setFont (juce::FontOptions (13.0f));
    info.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (info);

    midiMode.bind (globalParamID (gp::MidiMode));
    master.bind (globalParamID (gp::Master));
    addAndMakeVisible (midiMode);
    addAndMakeVisible (master);

    for (int v = 0; v < kNumVoices; ++v)
    {
        auto& b = voiceButtons[(size_t) v];
        b.setButtonText (juce::String (v + 1) + "  " + drumNoteName (v));
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2b7bb9));
        b.setTooltip ("Click to select and play. Drop a sample here to load it into voice " + juce::String (v + 1) + ".");
        b.onPress = [this, v]
        {
            selectVoice (v);
            proc.audition (v, true);
        };
        b.onRelease = [this, v] { proc.audition (v, false); };
        addAndMakeVisible (b);
    }

    const auto tabColour = juce::Colour (0xff2a2d31);
    tabs.addTab ("Source", tabColour, sourcePage = new SourcePage (p), true);
    tabs.addTab ("FM", tabColour, fmPage = new FmPage (p), true);
    tabs.addTab ("Chain", tabColour, chainPage = new ChainPage (p), true);
    tabs.addTab ("Envelopes & Play", tabColour, envelopePage = new EnvelopePage (p), true);
    tabs.setTabBarDepth (30);
    tabs.setComponentID ("tabs");
    tabs.setCurrentTabIndex (1);
    addAndMakeVisible (tabs);

    selectVoice (juce::jlimit (0, kNumVoices - 1, proc.selectedVoice.load()));

    setSize (980, 640);
    startTimerHz (4);
}

BatidaEditor::~BatidaEditor()
{
    stopTimer();
}

void BatidaEditor::selectVoice (int voice)
{
    if (voice == selected && sourcePage != nullptr && voiceButtons[(size_t) voice].getToggleState())
        return;

    selected = voice;
    proc.selectedVoice = voice;

    for (int v = 0; v < kNumVoices; ++v)
        voiceButtons[(size_t) v].setToggleState (v == voice, juce::dontSendNotification);

    sourcePage->bindVoice (voice);
    fmPage->bindVoice (voice);
    chainPage->bindVoice (voice);
    envelopePage->bindVoice (voice);
    updateInfo();
}

void BatidaEditor::updateInfo()
{
    const auto p = proc.readVoiceParams (selected);
    static const char* sources[] = { "FM", "Sample", "Layer" };
    const auto source = sources[juce::jlimit (0, 2, p.choice (vp::SrcMode))];

    auto& slot = proc.sampleSlot (selected);
    juce::String sample = "no sample";
    if (slot.getStatus() == SampleSlot::Status::Loaded)
        sample = juce::File (slot.getPath()).getFileName();
    else if (slot.getStatus() == SampleSlot::Status::Missing)
        sample = "sample missing";

    info.setText ("Voice " + juce::String (selected + 1) + "  |  drum-map note " + drumNoteName (selected)
                      + " (" + juce::String (kDrumMapFirstNote + selected) + "), Split mode: MIDI channel "
                      + juce::String (selected + 1) + "  |  " + source + ", " + sample
                      + "  |  default sound: " + defaultVoiceName (selected),
                  juce::dontSendNotification);
}

void BatidaEditor::timerCallback()
{
    updateInfo();

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto& slot = proc.sampleSlot (v);
        const auto version = slot.getVersion();
        if (version == lastSampleVersion[(size_t) v])
            continue;
        lastSampleVersion[(size_t) v] = version;

        auto text = juce::String (v + 1) + "  " + drumNoteName (v);
        if (slot.getStatus() == SampleSlot::Status::Missing)
            text << "  !";
        voiceButtons[(size_t) v].setButtonText (text);
    }
}

void BatidaEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e2023));

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("NEGATIVE SPACE", title.getBounds().translated (0, 24), juce::Justification::topLeft);

    if (dropTarget >= 0)
    {
        g.setColour (juce::Colours::orange);
        g.drawRect (voiceButtons[(size_t) dropTarget].getBounds().expanded (3), 2);
    }
}

void BatidaEditor::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto header = r.removeFromTop (ParamControl::kHeight);

    title.setBounds (header.removeFromLeft (150).removeFromTop (30));
    master.setBounds (header.removeFromRight (ParamControl::kWidth));
    midiMode.setBounds (header.removeFromRight (110));
    header.removeFromRight (10);

    auto buttons = header.removeFromTop (44);
    const auto w = buttons.getWidth() / kNumVoices;
    for (int v = 0; v < kNumVoices; ++v)
        voiceButtons[(size_t) v].setBounds (buttons.removeFromLeft (w).reduced (3, 2));
    info.setBounds (header.removeFromTop (30));

    r.removeFromTop (6);
    tabs.setBounds (r);
}

int BatidaEditor::voiceAt (int x, int y) const
{
    for (int v = 0; v < kNumVoices; ++v)
        if (voiceButtons[(size_t) v].getBounds().expanded (3).contains (x, y))
            return v;
    return -1;
}

bool BatidaEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isAudioFile (f))
            return true;
    return false;
}

void BatidaEditor::fileDragEnter (const juce::StringArray& files, int x, int y)
{
    fileDragMove (files, x, y);
}

void BatidaEditor::fileDragMove (const juce::StringArray&, int x, int y)
{
    const auto target = voiceAt (x, y);
    const auto shown = target >= 0 ? target : selected;
    if (shown != dropTarget)
    {
        dropTarget = shown;
        repaint();
    }
}

void BatidaEditor::fileDragExit (const juce::StringArray&)
{
    dropTarget = -1;
    repaint();
}

void BatidaEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    const auto target = voiceAt (x, y);
    const auto voice = target >= 0 ? target : selected;
    dropTarget = -1;
    repaint();

    for (const auto& f : files)
    {
        if (! isAudioFile (f))
            continue;

        proc.loadSample (voice, juce::File (f));
        selectVoice (voice);
        sourcePage->bindVoice (voice);
        tabs.setCurrentTabIndex (0);
        break;
    }
}

juce::AudioProcessorEditor* BatidaProcessor::createEditor()
{
    return new BatidaEditor (*this);
}
