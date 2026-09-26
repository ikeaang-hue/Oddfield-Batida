#pragma once

#include "Plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Save, load and init at every level (sound, kit, pattern, set), stepping
// through the library, and the Save and Relink panels. Owned by the editor;
// the panels are drawn over it.

// Saving to the library: name, category, tags, author, Collect samples.
class SavePanel final : public juce::Component
{
public:
    SavePanel (BatidaProcessor& p, batida::PresetType type, int voice, int pattern);

    std::function<void()> onClose;
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {} // clicks outside the box don't fall through

private:
    batida::PresetInfo currentInfo() const;
    void update();
    bool saveTo (const juce::File& file);
    void saveElsewhere();

    BatidaProcessor& proc;
    const batida::PresetType type;
    const int voice, pattern;
    juce::Label title, nameLabel, categoryLabel, tagsLabel, authorLabel, where, message;
    juce::TextEditor name, author;
    juce::ComboBox category; // the model; chosen with the chips
    std::array<juce::TextButton, 7> categories;
    std::array<juce::TextButton, 5> tags;
    juce::String tagText, titleText;
    juce::ToggleButton collect { "Collect samples (copy them next to the file)" };
    juce::TextButton save { "SAVE" }, elsewhere { "SAVE ELSEWHERE..." }, cancel { "CANCEL" };
    juce::File replacing; // armed: the next Save replaces this file
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> box;
};

// Missing samples: search the known folders, a folder of your choice, or
// locate each one.
class RelinkPanel final : public juce::Component, private juce::Timer
{
public:
    explicit RelinkPanel (BatidaProcessor& p);
    ~RelinkPanel() override;

    std::function<void()> onClose;
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}

private:
    struct Search
    {
        std::atomic<bool> stop { false }, done { false };
        std::vector<std::pair<int, juce::File>> found;
        std::array<juce::String, batida::kNumVoices> wantedPaths; // what each slot was missing when the search began
    };

    void timerCallback() override;
    void rebuild();
    void searchFolder();
    void locate (int voice);

    BatidaProcessor& proc;
    juce::Label title, status;
    juce::OwnedArray<juce::Label> rowLabels;
    juce::OwnedArray<juce::TextButton> rowButtons;
    juce::TextButton searchButton { "SEARCH IN FOLDER..." }, againButton { "LOOK AGAIN" }, closeButton { "LATER" };
    std::shared_ptr<Search> search;
    std::unique_ptr<juce::FileChooser> chooser;
    int shownMissing = -1;
    juce::Rectangle<int> box;
};

class LibraryController
{
public:
    LibraryController (BatidaProcessor& p, juce::Component& overlayParent);
    ~LibraryController();

    // The menu for one level (the ◀ name ▶ strips' name button).
    void showMenu (batida::PresetType type, juce::Component& target);
    void step (batida::PresetType type, int delta); // next / previous in the LIB list
    juce::String currentName (batida::PresetType type) const;
    juce::File currentFile (batida::PresetType type) const;

    // Try a library file (a browse load: one undo step for a run of them).
    void tryFile (const juce::File& file);
    void trySample (const juce::File& file);

    void init (batida::PresetType type);
    void loadFromDisk (batida::PresetType type);
    void save (batida::PresetType type); // back to its own User file, else Save As
    void saveAs (batida::PresetType type);
    void showRelink();
    void collectSamples();

    void layoutOverlays();
    std::function<void()> onChanged; // after a load: the editor refreshes

    int voice() const { return proc.selectedVoice.load(); }
    int pattern() const { return proc.displayPattern(); }

private:
    void auditionOnce (int voice);
    void closeOverlays();

    BatidaProcessor& proc;
    juce::Component& parent;
    std::unique_ptr<SavePanel> savePanel;
    std::unique_ptr<RelinkPanel> relinkPanel;
    std::unique_ptr<juce::FileChooser> chooser;
    JUCE_DECLARE_WEAK_REFERENCEABLE (LibraryController)
};
