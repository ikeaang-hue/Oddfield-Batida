#pragma once

#include "BrowseStrip.h"
#include "LibraryController.h"
#include "Widgets.h"

// The LIB view: browse sounds, kits, patterns, sets and sample folders.
// Click a row to try it (live, in the beat); a run of clicks is one undo step.
class LibraryPage final : public juce::Component,
                          private juce::ListBoxModel,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    LibraryPage (BatidaProcessor& p, LibraryController& controller);
    ~LibraryPage() override;

    enum class Mode { Sounds, Kits, Patterns, Sets, Samples };
    void setMode (Mode mode);
    Mode getMode() const { return mode; }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    bool keyPressed (const juce::KeyPress& key) override;

    // Tries in this browsing run (one Undo takes them all back); reset by Undo.
    void resetTries() { tries = 0; repaint(); }

    // For the gesture checks: click row `row` as a user would (x from the left).
    void clickRow (int row, int x = 100);
    int getNumShown() { return getNumRows(); }

private:
    struct Row final : juce::Component
    {
        LibraryPage& page;
        int row = -1;
        explicit Row (LibraryPage& p) : page (p) {}
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override { page.clickRow (row, e.x); }
    };

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;
    void selectedRowsChanged (int row) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
    void timerCallback() override;

    batida::PresetType presetType() const { return (batida::PresetType) juce::jlimit (0, 3, (int) mode); }
    batida::LibraryFilter& filter() { return proc.library().filterFor (presetType()); }
    void rebuild();
    void updateFilterButtons();
    void updateDetails();
    void updateInfo (juce::File file, const batida::PresetInfo& info); // a User file's name, category or tags
    void tryRow (int row);
    void step (int delta);
    bool isCurrent (int row) const;
    void decide (batida::Decision decision); // Review: keep or reject the selected one, then try the next

    // Samples mode
    void setFolder (const juce::File& folder);
    void rebuildRoots();
    void addFolder();

    BatidaProcessor& proc;
    LibraryController& controller;
    Mode mode = Mode::Sounds;
    std::vector<batida::LibraryEntry> rows;
    std::vector<juce::File> sampleRows; // folders first, then audio files
    juce::File folder;
    int shownRow = -1, shownVersion = -1, shownOrigins = -1, tries = 0;
    bool selecting = false;

    std::array<juce::TextButton, 5> modeButtons;
    std::array<juce::TextButton, 5> sourceButtons; // ALL FACTORY USER ♥, and REVIEW in review builds
    juce::TextButton keepButton { "KEEP" }, rejectButton { "REJECT" };
    std::array<juce::TextButton, 8> categoryButtons; // All + 7
    std::array<juce::TextButton, 5> tagButtons;
    juce::TextEditor search;
    juce::Label countLabel, hint, detailName, detailWhat, detailInfo, detailPath, showLabel;
    juce::ListBox list { "library", this };
    TextLink previous { juce::String::fromUTF8 ("\xe2\x80\xb9 PREV"), true }, next { juce::String::fromUTF8 ("NEXT \xe2\x80\xba"), true };
    TextLink loadFile { juce::String::fromUTF8 ("LOAD\xe2\x80\xa6"), true }, saveAs { juce::String::fromUTF8 ("SAVE AS\xe2\x80\xa6"), true },
        init { "INIT", true }, reveal { "SHOW LIBRARY IN FINDER", true };
    juce::Rectangle<int> detailBounds;
    std::array<juce::TextButton, 5> detailTags;
    juce::ComboBox detailCategory;
    juce::ComboBox rootBox;
    juce::TextButton upButton { "UP" }, addFolderButton { "ADD FOLDER..." }, removeFolderButton { "REMOVE" };
    std::unique_ptr<juce::FileChooser> chooser;
};
