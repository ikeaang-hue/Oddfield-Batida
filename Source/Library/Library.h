#pragma once

#include "PresetFiles.h"

#include <juce_events/juce_events.h>

// The library folder on disk:
//
//   ~/Music/Negative Space/Batida/
//     Factory/  Sounds/<Category>/, Kits/, Patterns/, Sets/   (written by Batida)
//     User/     the same layout, for the user's own files
//     Favourites.xml
//
// Batida writes the factory files itself and never edits User files except
// when asked (save, tags). The index is read from disk in the background.
// Message thread only, apart from the background scan.

namespace batida
{

struct LibraryEntry
{
    juce::File file;
    PresetType type = PresetType::Sound;
    bool factory = false;
    PresetInfo info;
};

struct LibraryFilter
{
    enum class Source { All, Factory, User, Favourites };
    Source source = Source::All;
    juce::String category;  // sounds: empty = any
    juce::StringArray tags; // every one must match
    juce::String search;    // every word must appear in the name, tags, category or author

    bool isDefault() const { return source == Source::All && category.isEmpty() && tags.isEmpty() && search.isEmpty(); }
};

class Library final : public juce::ChangeBroadcaster
{
public:
    Library(); // the user's library (see defaultRoot)
    Library (const juce::File& root, const juce::File& settingsFile);
    ~Library() override;

    // $BATIDA_LIBRARY (tests and tools), the location in the settings, or ~/Music/Negative Space/Batida.
    static juce::File defaultRoot();
    static juce::File defaultSettingsFile();

    const juce::File& getRoot() const { return root; }
    juce::File folderFor (PresetType type, bool factory) const; // .../User/Sounds
    juce::File userFileFor (PresetType type, const PresetInfo& info) const;
    bool isFactory (const juce::File& file) const;
    static juce::String categoryFolder (const juce::String& category); // "fx" -> "FX", "" -> "Other"

    // Writes the factory files when they're missing or older than this
    // version. Never touches User. Call before first use.
    static constexpr int kFactoryVersion = 1;
    bool installFactory();
    void prepare(); // installFactory once, then refresh()

    // The index.
    void scanNow();  // reads on this thread
    void refresh();  // reads in the background, then sends a change message
    bool isScanning() const { return scanning; }
    const std::vector<LibraryEntry>& getEntries() const { return entries; }
    int getVersion() const { return version; }
    const LibraryEntry* entryFor (const juce::File& file) const;

    std::vector<LibraryEntry> filtered (PresetType type, const LibraryFilter& filter) const;
    LibraryFilter& filterFor (PresetType type) { return filters[(size_t) type]; }
    std::vector<LibraryEntry> shown (PresetType type) const { return filtered (type, filters[(size_t) type]); }

    // Favourites (kept in the library folder; factory files can be favourites too).
    bool isFavourite (const juce::File& file) const;
    void setFavourite (const juce::File& file, bool favourite);

    // Rewrites a User file's name, category and tags.
    bool updateInfo (const juce::File& file, const PresetInfo& info);

    // Settings.
    juce::String getAuthor() const { return author; }
    void setAuthor (const juce::String& name);
    int getZoom() const { return zoom; } // editor size in percent: 100, 125 or 150
    void setZoom (int percent);
    juce::Array<juce::File> getSampleFolders() const { return sampleFolders; }
    void addSampleFolder (const juce::File& folder);
    void removeSampleFolder (const juce::File& folder);
    void addRelinkFolder (const juce::File& folder);
    juce::Array<juce::File> searchFolders() const; // where missing samples are looked for

private:
    static std::vector<LibraryEntry> scanFolder (const juce::File& root);
    void loadSettings();
    void saveSettings() const;
    void loadFavourites();
    void saveFavourites() const;
    juce::String keyFor (const juce::File& file) const;

    juce::File root, settingsFile;
    std::vector<LibraryEntry> entries;
    std::array<LibraryFilter, kNumPresetTypes> filters {};
    juce::StringArray favourites; // paths relative to the root
    juce::String author;
    int zoom = 100;
    juce::Array<juce::File> sampleFolders, relinkFolders;
    int version = 0, scanGeneration = 0;
    bool scanning = false, prepared = false;
    juce::ThreadPool pool { 1 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (Library)
    JUCE_DECLARE_NON_COPYABLE (Library)
};

} // namespace batida
