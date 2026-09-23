#include "PluginEditor.h"

#include "RenameField.h"

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

void BatidaEditor::VoiceButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto base = findColour (getToggleState() ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId);
    getLookAndFeel().drawButtonBackground (g, *this, base, over, down);

    auto r = getLocalBounds().reduced (6, 2);
    g.setColour (audible ? juce::Colours::white : juce::Colours::white.withAlpha (0.35f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawFittedText (name, r.removeFromTop (r.getHeight() * 3 / 5), juce::Justification::centredBottom, 1, 0.7f);
    g.setColour (juce::Colours::lightgrey.withAlpha (audible ? 0.8f : 0.35f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (slot, r, juce::Justification::centredTop);

    if (missing)
    {
        g.setColour (juce::Colours::orange);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("!", getLocalBounds().reduced (5, 2), juce::Justification::topRight);
    }
    if (dropHover)
    {
        g.setColour (juce::Colours::orange);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 2.0f);
    }
}

void BatidaEditor::VoiceButton::mouseDown (const juce::MouseEvent& e)
{
    dragStarted = false;
    if (e.mods.isPopupMenu())
    {
        if (onMenu)
            onMenu();
        return;
    }
    juce::TextButton::mouseDown (e);
    if (onPress)
        onPress();
}

void BatidaEditor::VoiceButton::mouseDrag (const juce::MouseEvent& e)
{
    juce::TextButton::mouseDrag (e);
    if (dragStarted || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 8)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragStarted = true;
        container->startDragging ("voice:" + juce::String (index), this);
    }
}

void BatidaEditor::VoiceButton::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    juce::TextButton::mouseUp (e);
    if (onRelease)
        onRelease();
}

void BatidaEditor::VoiceButton::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onRename)
        onRename();
}

bool BatidaEditor::VoiceButton::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith ("voice:");
}

void BatidaEditor::VoiceButton::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    repaint();
    const auto from = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
    if (onSwapFrom && from != index)
        onSwapFrom (from);
}

BatidaEditor::BatidaEditor (BatidaProcessor& p)
    : AudioProcessorEditor (&p), proc (p), midiMode (p.getState()), keysVoice (p.getState()), master (p.getState()),
      kitPage (p), seqPage (p), modPage (p)
{
    title.setText ("BATIDA", juce::dontSendNotification);
    title.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    addAndMakeVisible (title);

    info.setFont (juce::FontOptions (13.0f));
    info.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (info);

    midiMode.bind (globalParamID (gp::MidiMode));
    keysVoice.bind (globalParamID (gp::KeysVoice));
    master.bind (globalParamID (gp::Master));
    addAndMakeVisible (midiMode);
    addAndMakeVisible (keysVoice);
    addAndMakeVisible (master);

    for (int v = 0; v < kNumVoices; ++v)
    {
        auto& b = voiceButtons[(size_t) v];
        b.index = v;
        b.slot = juce::String (v + 1) + " \u00b7 " + drumNoteName (v);
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2b7bb9));
        b.setTooltip ("Click: select and play. Double-click: rename. Drag onto another voice: swap slots. "
                      "Right-click: menu. Drop a sample here to load it.");
        b.onPress = [this, v]
        {
            selectVoice (v);
            proc.setKeysVoice (v); // only on a click, so opening the editor never changes it
            proc.audition (v, true);
        };
        b.onRelease = [this, v] { proc.audition (v, false); };
        b.onRename = [this, v] { renameVoice (v); };
        b.onMenu = [this, v] { voiceMenu (v); };
        b.onSwapFrom = [this, v] (int from) { swapVoices (from, v); };
        addAndMakeVisible (b);

        auto& m = muteButtons[(size_t) v];
        auto& so = soloButtons[(size_t) v];
        m.setButtonText ("M");
        so.setButtonText ("S");
        m.setTooltip ("Mute voice " + juce::String (v + 1));
        so.setTooltip ("Solo voice " + juce::String (v + 1) + " (while any voice is soloed, only soloed voices sound)");
        m.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9534f));
        so.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0b84a));
        so.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        for (auto* t : { &m, &so })
        {
            t->setClickingTogglesState (true);
            addAndMakeVisible (*t);
        }
        muteAttachments[(size_t) v] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            p.getState(), voiceParamID (v, vp::Mute), m);
        soloAttachments[(size_t) v] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            p.getState(), voiceParamID (v, vp::Solo), so);
    }
    refreshVoiceButtons();

    const auto tabColour = juce::Colour (0xff2a2d31);
    tabs.addTab ("Source", tabColour, sourcePage = new SourcePage (p), true);
    tabs.addTab ("FM", tabColour, fmPage = new FmPage (p), true);
    tabs.addTab ("Voice FX", tabColour, chainPage = new ChainPage (p), true);
    tabs.addTab ("Envelopes & Play", tabColour, envelopePage = new EnvelopePage (p), true);
    tabs.addTab ("Vary", tabColour, varyPage = new VaryPage (p), true);
    tabs.setTabBarDepth (30);
    tabs.setComponentID ("tabs");
    tabs.setCurrentTabIndex (1);
    addChildComponent (tabs);

    kitPage.setComponentID ("kitPage");
    addAndMakeVisible (kitPage);
    seqPage.setComponentID ("seqPage");
    addChildComponent (seqPage);
    modPage.setComponentID ("modPage");
    addChildComponent (modPage);

    undoButton.setTooltip ("Undo pattern edits, names, Vary Keep, scene recall and slot swaps "
                           "(knob moves are undone in Logic)");
    redoButton.setTooltip ("Redo");
    undoButton.onClick = [this] { proc.undo(); refreshVoiceButtons(); };
    redoButton.onClick = [this] { proc.redo(); refreshVoiceButtons(); };
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);

    for (auto* b : { &kitViewButton, &seqViewButton, &modViewButton, &voiceViewButton })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc0632a));
        addAndMakeVisible (*b);
    }
    kitViewButton.onClick = [this] { showView (View::Kit); };
    seqViewButton.onClick = [this] { showView (View::Seq); };
    modViewButton.onClick = [this] { showView (View::Mod); };
    voiceViewButton.onClick = [this] { showView (View::Voice); };
    showView (View::Kit); // the kit panel is the opening page

    selectVoice (juce::jlimit (0, kNumVoices - 1, proc.selectedVoice.load()));

    setSize (980, 640);
    startTimerHz (10);
}

BatidaEditor::~BatidaEditor()
{
    stopTimer();
}

void BatidaEditor::showView (View view)
{
    kitPage.setVisible (view == View::Kit);
    seqPage.setVisible (view == View::Seq);
    modPage.setVisible (view == View::Mod);
    modViewButton.setToggleState (view == View::Mod, juce::dontSendNotification);
    tabs.setVisible (view == View::Voice);
    kitViewButton.setToggleState (view == View::Kit, juce::dontSendNotification);
    seqViewButton.setToggleState (view == View::Seq, juce::dontSendNotification);
    voiceViewButton.setToggleState (view == View::Voice, juce::dontSendNotification);
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
    varyPage->bindVoice (voice);
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

    const auto chromatic = (MidiMode) juce::roundToInt (proc.getState().getRawParameterValue (globalParamID (gp::MidiMode))->load())
                           == MidiMode::Chromatic;
    const auto keys = juce::roundToInt (proc.getState().getRawParameterValue (globalParamID (gp::KeysVoice))->load());
    const auto keysText = chromatic ? "keys play voice " + juce::String (keys + 1) + " (drum map on ch 10)"
                                    : juce::String ("keys: drum map");

    info.setText (juce::String (selected + 1) + " " + proc.getVoiceName (selected) + "  |  note " + drumNoteName (selected)
                      + " (" + juce::String (kDrumMapFirstNote + selected) + ")  |  " + source + ", " + sample
                      + "  |  " + keysText,
                  juce::dontSendNotification);
}

void BatidaEditor::timerCallback()
{
    updateInfo();
    refreshVoiceButtons();
    undoButton.setEnabled (proc.canUndo());
    redoButton.setEnabled (proc.canRedo());
}

void BatidaEditor::refreshVoiceButtons()
{
    // Names, missing samples, and whether each voice can be heard (mute/solo).
    bool anySolo = false;
    std::array<VoiceParams, kNumVoices> p;
    for (int v = 0; v < kNumVoices; ++v)
    {
        p[(size_t) v] = proc.readVoiceParams (v);
        anySolo = anySolo || p[(size_t) v].flag (vp::Solo);
    }

    for (int v = 0; v < kNumVoices; ++v)
    {
        auto& b = voiceButtons[(size_t) v];
        const auto name = proc.getVoiceName (v);
        const auto missing = proc.sampleSlot (v).getStatus() == SampleSlot::Status::Missing;
        const auto audible = ! p[(size_t) v].flag (vp::Mute) && (! anySolo || p[(size_t) v].flag (vp::Solo));
        if (name != b.name || missing != b.missing || audible != b.audible)
        {
            b.name = name;
            b.missing = missing;
            b.audible = audible;
            b.repaint();
        }
    }
}

void BatidaEditor::renameVoice (int voice)
{
    startRename (renameEditor, *this, voiceButtons[(size_t) voice].getBounds(), proc.getVoiceName (voice),
                 [this, voice] (const juce::String& text)
                 {
                     proc.setVoiceName (voice, text);
                     refreshVoiceButtons();
                     updateInfo();
                 });
}

void BatidaEditor::swapVoices (int from, int to)
{
    proc.swapVoices (from, to);
    selected = -1; // force the pages to re-read
    selectVoice (to);
    refreshVoiceButtons();
}

void BatidaEditor::voiceMenu (int voice)
{
    juce::PopupMenu swapMenu;
    for (int v = 0; v < kNumVoices; ++v)
        if (v != voice)
            swapMenu.addItem (100 + v, juce::String (v + 1) + " " + proc.getVoiceName (v));

    juce::PopupMenu m;
    m.addSectionHeader (juce::String (voice + 1) + " " + proc.getVoiceName (voice));
    m.addItem (1, "Rename...");
    m.addSubMenu ("Swap with", swapMenu);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (voiceButtons[(size_t) voice]),
                     [this, voice] (int choice)
                     {
                         if (choice == 1)
                             renameVoice (voice);
                         else if (choice >= 100)
                             swapVoices (voice, choice - 100);
                     });
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

    auto titleArea = header.removeFromLeft (196);
    title.setBounds (titleArea.removeFromTop (30));
    auto views = titleArea.removeFromBottom (28).withTrimmedRight (6);
    const auto bw = views.getWidth() / 4;
    kitViewButton.setBounds (views.removeFromLeft (bw).withTrimmedRight (2));
    seqViewButton.setBounds (views.removeFromLeft (bw).withTrimmedRight (2));
    modViewButton.setBounds (views.removeFromLeft (bw).withTrimmedRight (2));
    voiceViewButton.setBounds (views);
    master.setBounds (header.removeFromRight (ParamControl::kWidth));
    keysVoice.setBounds (header.removeFromRight (80));
    midiMode.setBounds (header.removeFromRight (110));
    header.removeFromRight (10);

    auto buttons = header.removeFromTop (38);
    auto ms = header.removeFromTop (22);
    const auto w = buttons.getWidth() / kNumVoices;
    for (int v = 0; v < kNumVoices; ++v)
    {
        voiceButtons[(size_t) v].setBounds (buttons.removeFromLeft (w).reduced (3, 1));
        auto cell = ms.removeFromLeft (w).reduced (3, 2);
        muteButtons[(size_t) v].setBounds (cell.removeFromLeft (cell.getWidth() / 2).withTrimmedRight (1));
        soloButtons[(size_t) v].setBounds (cell.withTrimmedLeft (1));
    }
    redoButton.setBounds (header.removeFromRight (52).reduced (2, 3));
    undoButton.setBounds (header.removeFromRight (52).reduced (2, 3));
    info.setBounds (header);

    r.removeFromTop (6);
    tabs.setBounds (r);
    kitPage.setBounds (r);
    seqPage.setBounds (r);
    modPage.setBounds (r);
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
        showView (View::Voice);
        break;
    }
}

juce::AudioProcessorEditor* BatidaProcessor::createEditor()
{
    return new BatidaEditor (*this);
}
