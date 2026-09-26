#pragma once

#include "MouseLog.h"

#include "Frame.h"
#include "KitPage.h"
#include "LibraryPage.h"
#include "ModPage.h"
#include "SeqPage.h"
#include "SoundPage.h"

// Batida's editor, direction C "Signal": the top bar and sound row around
// five pages (KIT, SEQ, MOD, SOUND, LIB). Everything is laid out at 980×640
// inside one content component that is scaled for the 100/125/150% zoom.
// Kept apart from the engine: it only talks to the processor.
class BatidaEditor final : public juce::AudioProcessorEditor,
                           public juce::FileDragAndDropTarget,
                           public juce::DragAndDropContainer,
                           private juce::Timer
{
public:
    explicit BatidaEditor (BatidaProcessor&);
    ~BatidaEditor() override;

    static constexpr int kWidth = 980, kHeight = 640;
    enum Page { Kit, Seq, Mod, Sound, Lib };
    void showPage (int page);
    int getPage() const { return page; }
    void setZoom (int percent);
    juce::Component& getContent() { return content; }
    void selectVoice (int voice);

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    struct Content final : juce::Component
    {
        void paint (juce::Graphics& g) override { g.fillAll (theme::bg); }
    };
    struct Scrim final : juce::Component
    {
        std::function<void()> onClick;
        void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    };

    void timerCallback() override;
    void wireCell (SoundCell& cell, bool inSequencer);
    void refreshCells();
    void renameVoice (int voice, juce::Component& over);
    void swapVoices (int from, int to);
    void voiceMenu (int voice, juce::Component& target);
    void toggleMute (int voice, int param);
    void updateStrips();
    void libraryChanged();
    int voiceAt (juce::Point<int> contentPoint) const;
    bool isOverGrid (juce::Point<int> contentPoint);
    void setDropTarget (int voice);
    std::vector<SoundCell*> allCells();

    juce::SharedResourcePointer<theme::SharedLook> look;
    BatidaProcessor& proc;
    Content content;
    LibraryController library; // before the pages that use it

    TopBar top;
    SoundRow row;
    KitPage kitPage;
    SeqPage seqPage;
    ModPage modPage;
    SoundPage soundPage;
    LibraryPage libraryPage;
    Scrim settingsScrim;
    SettingsPanel settings;
    AboutCard about;
    juce::TooltipWindow tooltips { &content, 700 };
    std::unique_ptr<juce::TextEditor> renameEditor;
    std::unique_ptr<MouseLog> mouseLog; // review builds only

    int selected = 0, page = Kit, dropTarget = -1, zoom = 100, ticks = 0;
    std::array<uint32_t, batida::kNumVoices> hitCounts {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BatidaEditor)
};
