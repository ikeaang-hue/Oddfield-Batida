#pragma once

#include "FmPage.h"
#include "KitPage.h"
#include "LibraryPage.h"
#include "ModPage.h"
#include "SeqPage.h"
#include "VaryPage.h"
#include "VoicePages.h"

// Minimal demo UI (stock JUCE controls). Views: KIT, the main panel for the
// whole kit (XY pad, chain, Chain amounts), which opens first; SEQ, the
// sequencer; MOD, the modulators; VOICE, the selected sound's Source / FM /
// Voice FX / Envelope / Vary pages; LIB, the library. ◀ name ▶ strips step
// through the library for the kit (header), sound (VOICE) and pattern (SEQ).
// Above them: the voice buttons (select + audition; double-click to rename;
// drag onto another to swap slots; right-click for a menu) with a Mute and
// Solo button under each. Kept apart from the engine so a proper
// visual design can replace it later.
class BatidaEditor final : public juce::AudioProcessorEditor,
                           public juce::FileDragAndDropTarget,
                           public juce::DragAndDropContainer,
                           private juce::Timer
{
public:
    explicit BatidaEditor (BatidaProcessor&);
    ~BatidaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    // Shows the voice's name and slot; plays the voice while held.
    struct VoiceButton final : juce::TextButton, juce::DragAndDropTarget
    {
        int index = 0;
        juce::String name, slot;
        bool missing = false, audible = true, dropHover = false, dragStarted = false;
        std::function<void()> onPress, onRelease, onRename, onMenu;
        std::function<void (int from)> onSwapFrom;

        void paintButton (juce::Graphics& g, bool over, bool down) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;

        bool isInterestedInDragSource (const SourceDetails& d) override;
        void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
        void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
        void itemDropped (const SourceDetails& d) override;
    };

    void selectVoice (int voice);
    enum class View { Kit, Seq, Mod, Voice, Lib };
    void showView (View view);
    int voiceAt (int x, int y) const;
    void timerCallback() override;
    void updateInfo();
    void refreshVoiceButtons();
    void renameVoice (int voice);
    void swapVoices (int from, int to);
    void voiceMenu (int voice);
    void updateStrips();
    void libraryChanged();

    BatidaProcessor& proc;
    LibraryController library; // before the pages that use it

    juce::Label title, info;
    ParamControl midiMode, keysVoice, master;
    std::array<VoiceButton, batida::kNumVoices> voiceButtons;
    std::array<juce::TextButton, batida::kNumVoices> muteButtons, soloButtons;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, batida::kNumVoices> muteAttachments,
        soloAttachments;
    juce::TextButton kitViewButton { "KIT" }, seqViewButton { "SEQ" }, modViewButton { "MOD" }, voiceViewButton { "VOICE" },
        libViewButton { "LIB" };
    BrowseStrip kitStrip, soundStrip;
    juce::TextButton missingButton;
    View currentView = View::Kit;
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    KitPage kitPage;
    SeqPage seqPage;
    ModPage modPage;
    LibraryPage libraryPage;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<juce::TextEditor> renameEditor;

    SourcePage* sourcePage = nullptr;
    FmPage* fmPage = nullptr;
    ChainPage* chainPage = nullptr;
    EnvelopePage* envelopePage = nullptr;
    VaryPage* varyPage = nullptr;

    int selected = 0;
    int dropTarget = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BatidaEditor)
};
