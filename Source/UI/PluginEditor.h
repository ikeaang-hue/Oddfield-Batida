#pragma once

#include "FmPage.h"
#include "KitPage.h"
#include "VoicePages.h"

// Minimal demo UI (stock JUCE controls). Two views: KIT, the main panel for
// the whole kit (XY pad, chain, Chain amounts), which opens first; and VOICE,
// the selected sound's Source / FM / Voice FX / Envelope pages. The voice
// buttons (select + audition) sit above both. Kept apart from the engine so a
// proper visual design can replace it later.
class BatidaEditor final : public juce::AudioProcessorEditor,
                           public juce::FileDragAndDropTarget,
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
    // Selects on press and plays the voice while held.
    struct VoiceButton final : juce::TextButton
    {
        std::function<void()> onPress, onRelease;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
    };

    void selectVoice (int voice);
    void showView (bool kit);
    int voiceAt (int x, int y) const;
    void timerCallback() override;
    void updateInfo();

    BatidaProcessor& proc;

    juce::Label title, info;
    ParamControl midiMode, keysVoice, master;
    std::array<VoiceButton, batida::kNumVoices> voiceButtons;
    juce::TextButton kitViewButton { "KIT" }, voiceViewButton { "VOICE" };
    KitPage kitPage;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    SourcePage* sourcePage = nullptr;
    FmPage* fmPage = nullptr;
    ChainPage* chainPage = nullptr;
    EnvelopePage* envelopePage = nullptr;

    int selected = 0;
    int dropTarget = -1;
    std::array<int, batida::kNumVoices> lastSampleVersion {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BatidaEditor)
};
