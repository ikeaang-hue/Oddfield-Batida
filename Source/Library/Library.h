#pragma once

#include "PresetFiles.h"

#include <juce_events/juce_events.h>

// The library folder on disk:
//
//   ~/Music/Oddfield/Batida/
//     Factory/  Sounds/<Category>/, Kits/, Patterns/, Sets/, Samples/   (written by Batida)
//     User/     the same layout, for the user's own files
//     Review/   candidates from the preset factory, for listening (review builds only)
//     Favourites.xml
//
// The factory is built into the plugin as an archive and written out when
// it's missing or older; Batida never edits User files except when asked
// (save, tags). The index is read from disk in the background. Message
// thread only, apart from the background scan.

// Review builds (-DBATIDA_REVIEW=ON) show the preset factory's candidates in
// LIB, with Keep and Reject (SPEC §11). Release builds leave Review alone.
#ifndef BATIDA_REVIEW
 #define BATIDA_REVIEW 0
#endif

namespace batida
{

struct LibraryEntry
{
    juce::File file;
    PresetType type = PresetType::Sound;
    bool factory = false;
    bool review = false;  // a preset factory candidate (review builds)
    PresetInfo info;
};

enum class Decision { None, Keep, Reject };

struct LibraryFilter
{
    enum class Source { All, Factory, User, Favourites, Review };
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

    // $BATIDA_LIBRARY (tests and tools), the location in the settings, or ~/Music/Oddfield/Batida.
    static juce::File defaultRoot();
    static juce::File defaultSettingsFile();

    const juce::File& getRoot() const { return root; }
    juce::File folderFor (PresetType type, bool factory) const; // .../User/Sounds
    juce::File userFileFor (PresetType type, const PresetInfo& info) const;
    bool isFactory (const juce::File& file) const;
    static juce::String categoryFolder (const juce::String& category); // "fx" -> "FX", "" -> "Other"

    // The factory archive (a zip built into the plugin, with its version in
    // factory-version.txt). Set once at startup, before the first prepare().
    static void setFactoryArchive (const void* data, size_t size);
    static int archiveVersion(); // 0 without an archive

    // Writes the factory when it's missing or older than the archive: the
    // Factory folder is Batida's, so it's replaced whole. Never touches User.
    bool installFactory();
    void prepare(); // installFactory once, then refresh()

    // Review (review builds): candidates in Review/, and what was decided.
    static constexpr bool kReviewBuild = BATIDA_REVIEW != 0;
    bool hasReview() const { return kReviewBuild && root.getChildFile ("Review").isDirectory(); }
    bool isReview (const juce::File& file) const { return file.isAChildOf (root.getChildFile ("Review")); }
    Decision getDecision (const juce::File& file) const;
    void setDecision (const juce::File& file, Decision decision);

    // The index.
    void scanNow();  // reads on this thread
    void refresh();  // reads in the background, then sends a change message
    bool isScanning() const { return scanning; }
    const std::vector<LibraryEntry>& getEntries() const { return entries; }
    int getVersion() const { return version; }
    const LibraryEntry* entryFor (const juce::File& file) const;

    // While anyone watches (an open editor), the User folder is checked every
    // 2 s in the background, and a new, changed or removed file refreshes the
    // index (SPEC §10). Counted, so two editors can watch at once.
    void startWatching();
    void stopWatching();
    bool isWatching() const { return watcher != nullptr; }
    // A number that changes when any Batida file under `folder` is added,
    // removed, resized or modified.
    static juce::int64 folderSignature (const juce::File& folder);

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
    void loadDecisions();
    void saveDecisions() const;
    juce::String keyFor (const juce::File& file) const;

    juce::File root, settingsFile;
    std::vector<LibraryEntry> entries;
    std::array<LibraryFilter, kNumPresetTypes> filters {};
    juce::StringArray favourites; // paths relative to the root
    juce::NamedValueSet decisions; // Review: path relative to Review → "keep" | "reject"
    juce::String author;
    int zoom = 100;
    juce::Array<juce::File> sampleFolders, relinkFolders;
    int version = 0, scanGeneration = 0;
    bool scanning = false, prepared = false;
    juce::ThreadPool pool { 1 };
    class Watcher;
    std::unique_ptr<Watcher> watcher;
    int watchers = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Library)
    JUCE_DECLARE_NON_COPYABLE (Library)
};

} // namespace batida
