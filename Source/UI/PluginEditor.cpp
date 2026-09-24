#include "PluginEditor.h"

#include "Library/SampleLocator.h"
#include "RenameField.h"

using namespace batida;

namespace
{
bool isAudioFile (const juce::String& path)
{
    return isAudioFileName (path);
}

bool isPresetFile (const juce::String& path)
{
    return presetTypeOf (juce::File (path)).has_value();
}

juce::String drumNoteName (int voice)
{
    return juce::MidiMessage::getMidiNoteName (kDrumMapFirstNote + voice, true, true, 3);
}

constexpr int kSide = 16;
} // namespace

BatidaEditor::BatidaEditor (BatidaProcessor& p)
    : AudioProcessorEditor (&p), proc (p), library (p, content), top (p), kitPage (p), seqPage (p), modPage (p), soundPage (p),
      libraryPage (p, library), settings (p)
{
    setLookAndFeel (&look->look);
    juce::LookAndFeel::setDefaultLookAndFeel (&look->look);
    addAndMakeVisible (content);

    top.setComponentID ("topBar");
    row.setComponentID ("soundRow");
    kitPage.setComponentID ("kitPage");
    seqPage.setComponentID ("seqPage");
    modPage.setComponentID ("modPage");
    soundPage.setComponentID ("soundPage");
    libraryPage.setComponentID ("libraryPage");
    settings.setComponentID ("settings");
    for (juce::Component* c : { (juce::Component*) &top, (juce::Component*) &row, (juce::Component*) &kitPage })
        content.addAndMakeVisible (c);
    for (juce::Component* c : { (juce::Component*) &seqPage, (juce::Component*) &modPage, (juce::Component*) &soundPage,
                                (juce::Component*) &libraryPage, (juce::Component*) &settingsScrim, (juce::Component*) &settings })
        content.addChildComponent (c);

    // Top bar
    top.onPage = [this] (int index) { showPage (index); };
    top.undo.onClick = [this] { proc.undo(); libraryPage.resetTries(); libraryChanged(); };
    top.redo.onClick = [this] { proc.redo(); libraryChanged(); };
    top.onRelink = [this] { library.showRelink(); };
    top.onSettings = [this]
    {
        const auto show = ! settings.isVisible();
        settings.refresh();
        settingsScrim.setVisible (show);
        settings.setVisible (show);
        if (show)
            settings.toFront (false);
    };
    settingsScrim.onClick = [this] { settingsScrim.setVisible (false); settings.setVisible (false); };
    settings.onZoom = [this] (int percent) { setZoom (percent); };

    // Library strips: the kit (top bar), the sound (SOUND) and the pattern (SEQ).
    auto wire = [this] (BrowseStrip& strip, PresetType type)
    {
        strip.onPrevious = [this, type] { library.step (type, -1); };
        strip.onNext = [this, type] { library.step (type, 1); };
        strip.onMenu = [this, &strip, type] { library.showMenu (type, strip); };
    };
    wire (top.kitStrip, PresetType::Kit);
    wire (soundPage.soundStrip, PresetType::Sound);
    wire (seqPage.patternStrip, PresetType::Pattern);
    library.onChanged = [this] { libraryChanged(); };
    soundPage.onSaveSound = [this] { library.saveAs (PresetType::Sound); };

    // Sound cells: the row, and the SEQ track headers (same gestures).
    for (auto* c : row.getCells())
        wireCell (*c, false);
    for (auto* c : seqPage.getTrackCells())
        wireCell (*c, true);
    seqPage.getGrid().onRename = [this] (int track) { renameVoice (track, *seqPage.getTrackCells()[(size_t) track]); };

    for (int v = 0; v < kNumVoices; ++v)
        hitCounts[(size_t) v] = proc.getKit().getHitCount (v);

    selectVoice (juce::jlimit (0, kNumVoices - 1, proc.selectedVoice.load()));
    showPage (Kit);
    updateStrips();
    refreshCells();

    zoom = proc.library().getZoom();
    setSize (kWidth * zoom / 100, kHeight * zoom / 100);
    startTimerHz (30);
}

BatidaEditor::~BatidaEditor()
{
    stopTimer();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void BatidaEditor::setZoom (int percent)
{
    zoom = juce::jlimit (100, 150, percent);
    setSize (kWidth * zoom / 100, kHeight * zoom / 100);
}

void BatidaEditor::wireCell (SoundCell& cell, bool inSequencer)
{
    const auto v = cell.index;
    cell.onPress = [this, v]
    {
        selectVoice (v);
        proc.setKeysVoice (v); // only on a click, so opening the editor never changes it
        proc.audition (v, true);
    };
    cell.onRelease = [this, v] { proc.audition (v, false); };
    cell.onRename = [this, v, &cell] { renameVoice (v, cell); };
    cell.onSwapFrom = [this, v] (int from) { swapVoices (from, v); };
    cell.onMute = [this, v] { toggleMute (v, vp::Mute); };
    cell.onSolo = [this, v] { toggleMute (v, vp::Solo); };
    if (inSequencer)
        cell.onMenu = [this, v, &cell] { selectVoice (v); seqPage.getGrid().trackMenu (v, cell); };
    else
        cell.onMenu = [this, v, &cell] { voiceMenu (v, cell); };
}

void BatidaEditor::toggleMute (int voice, int param)
{
    if (auto* p = proc.getState().getParameter (voiceParamID (voice, param)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() >= 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
    refreshCells();
}

std::vector<SoundCell*> BatidaEditor::allCells()
{
    std::vector<SoundCell*> cells;
    for (auto* c : row.getCells())
        cells.push_back (c);
    for (auto* c : seqPage.getTrackCells())
        cells.push_back (c);
    return cells;
}

void BatidaEditor::showPage (int p)
{
    page = p;
    kitPage.setVisible (p == Kit);
    seqPage.setVisible (p == Seq);
    modPage.setVisible (p == Mod);
    soundPage.setVisible (p == Sound);
    libraryPage.setVisible (p == Lib);
    row.setVisible (p != Seq); // the SEQ track headers are the sound row there
    if (p != Sound)
        soundPage.closeOverlays();
    top.setPage (p);
    resized();
}

void BatidaEditor::selectVoice (int voice)
{
    selected = voice;
    proc.selectedVoice = voice;
    soundPage.bindVoice (voice);
    refreshCells();
}

void BatidaEditor::refreshCells()
{
    refreshSoundCells (proc, row.getCells(), selected, [] (int v) { return drumNoteName (v); });
    const auto& pat = proc.patterns().get().patterns[(size_t) std::clamp (proc.displayPattern(), 0, kNumPatterns - 1)];
    refreshSoundCells (proc, seqPage.getTrackCells(), selected, [&pat] (int v)
    {
        return "LEN " + juce::String (std::min (pat.tracks[(size_t) v].length, pat.length));
    });
}

void BatidaEditor::updateStrips()
{
    const auto& o = proc.getOrigins();
    top.kitStrip.setText (o.kitName, "Kit: " + o.kitName + (o.kitFile.existsAsFile() ? " (" + o.kitFile.getFileName() + ")" : juce::String()));
    const auto pattern = proc.displayPattern();
    seqPage.patternStrip.setText ("P" + juce::String (pattern + 1).paddedLeft ('0', 2), "Pattern " + juce::String (pattern + 1));
    top.setMissing (proc.numMissingSamples());
}

void BatidaEditor::libraryChanged()
{
    // A load replaced sounds: the pages re-read the selected sound.
    selectVoice (selected);
    updateStrips();
}

void BatidaEditor::timerCallback()
{
    // Hit lights and the meter every frame; the rest ten times a second.
    const auto& kit = proc.getKit();
    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto count = kit.getHitCount (v);
        if (count != hitCounts[(size_t) v])
        {
            hitCounts[(size_t) v] = count;
            row.getCells()[(size_t) v]->hit();
            seqPage.getTrackCells()[(size_t) v]->hit();
        }
    }
    const auto peaks = proc.takeOutputPeaks();
    top.setOutputPeak (std::max (peaks[0], peaks[1]));

    if (++ticks % 3 != 0)
        return;
    refreshCells();
    updateStrips();
    top.undo.setEnabled (proc.canUndo());
    top.redo.setEnabled (proc.canRedo());
    if (settings.isVisible())
        settings.refresh();
}

void BatidaEditor::renameVoice (int voice, juce::Component& over)
{
    const auto area = content.getLocalArea (&over, over.getLocalBounds()).withHeight (std::min (22, over.getHeight()));
    startRename (renameEditor, content, area, proc.getVoiceName (voice),
                 [this, voice] (const juce::String& text)
                 {
                     proc.setVoiceName (voice, text);
                     refreshCells();
                 });
}

void BatidaEditor::swapVoices (int from, int to)
{
    proc.swapVoices (from, to);
    selectVoice (to);
}

void BatidaEditor::voiceMenu (int voice, juce::Component& target)
{
    juce::PopupMenu swapMenu;
    for (int v = 0; v < kNumVoices; ++v)
        if (v != voice)
            swapMenu.addItem (100 + v, juce::String (v + 1).paddedLeft ('0', 2) + " " + proc.getVoiceName (v));

    juce::PopupMenu m;
    m.addSectionHeader (juce::String (voice + 1).paddedLeft ('0', 2) + " " + proc.getVoiceName (voice));
    juce::PopupMenu::Item rename ("Rename...");
    rename.itemID = 1;
    rename.shortcutKeyDescription = "double-click";
    m.addItem (rename);
    m.addSubMenu ("Swap with", swapMenu);
    m.addSeparator();
    m.addItem (2, "Init sound");
    m.addItem (3, "Load sound...");
    m.addItem (4, "Save sound...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                     [this, voice, safe = juce::Component::SafePointer<juce::Component> (&target)] (int choice)
                     {
                         if (choice == 1 && safe != nullptr)
                             renameVoice (voice, *safe);
                         else if (choice >= 100)
                             swapVoices (voice, choice - 100);
                         else if (choice >= 2 && choice <= 4)
                         {
                             selectVoice (voice);
                             if (choice == 2)
                                 library.init (PresetType::Sound);
                             else if (choice == 3)
                                 library.loadFromDisk (PresetType::Sound);
                             else
                                 library.saveAs (PresetType::Sound);
                         }
                     });
}

void BatidaEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
}

void BatidaEditor::resized()
{
    content.setBounds (0, 0, kWidth, kHeight);
    content.setTransform (juce::AffineTransform::scale ((float) zoom / 100.0f));

    top.setBounds (kSide, 0, kWidth - 2 * kSide, 40);
    row.setBounds (kSide, 48, kWidth - 2 * kSide, 42);
    const auto pageTop = page == Seq ? 52 : 100;
    const auto area = juce::Rectangle<int> (kSide, pageTop, kWidth - 2 * kSide, kHeight - pageTop - 10);
    for (auto* p : { (juce::Component*) &kitPage, (juce::Component*) &seqPage, (juce::Component*) &modPage, (juce::Component*) &soundPage,
                     (juce::Component*) &libraryPage })
        p->setBounds (area);
    settingsScrim.setBounds (content.getLocalBounds());
    settings.setBounds (kWidth - kSide - SettingsPanel::kWidth, 36, SettingsPanel::kWidth, SettingsPanel::kHeight);
    library.layoutOverlays();
}

int BatidaEditor::voiceAt (juce::Point<int> p) const
{
    auto* self = const_cast<BatidaEditor*> (this);
    for (auto* c : self->allCells())
        if (c->isShowing() && content.getLocalArea (c, c->getLocalBounds()).expanded (2).contains (p))
            return c->index;
    return -1;
}

void BatidaEditor::setDropTarget (int voice)
{
    if (voice == dropTarget)
        return;
    dropTarget = voice;
    for (auto* c : allCells())
        c->setDropHighlight (c->index == voice && c->isShowing());
}

bool BatidaEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isAudioFile (f) || isPresetFile (f))
            return true;
    return false;
}

void BatidaEditor::fileDragEnter (const juce::StringArray& files, int x, int y)
{
    fileDragMove (files, x, y);
}

void BatidaEditor::fileDragMove (const juce::StringArray&, int x, int y)
{
    const auto target = voiceAt (content.getLocalPoint (this, juce::Point<int> (x, y)));
    setDropTarget (target >= 0 ? target : selected);
}

void BatidaEditor::fileDragExit (const juce::StringArray&)
{
    setDropTarget (-1);
}

void BatidaEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    const auto target = voiceAt (content.getLocalPoint (this, juce::Point<int> (x, y)));
    const auto voice = target >= 0 ? target : selected;
    setDropTarget (-1);

    for (const auto& f : files)
    {
        // A library file: a sound goes to the slot it's dropped on; the rest load whole.
        if (isPresetFile (f))
        {
            juce::String error;
            if (proc.loadPresetFile (juce::File (f), voice, proc.displayPattern(), BatidaProcessor::LoadMode::Step, &error))
            {
                if (presetTypeOf (juce::File (f)) == PresetType::Sound)
                    selectVoice (voice);
                libraryChanged();
            }
            else
                juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Couldn't load").withMessage (error).withButton ("OK"), nullptr);
            break;
        }
        if (! isAudioFile (f))
            continue;

        proc.loadSample (voice, juce::File (f));
        selectVoice (voice);
        showPage (Sound);
        break;
    }
}

juce::AudioProcessorEditor* BatidaProcessor::createEditor()
{
    return new BatidaEditor (*this);
}
