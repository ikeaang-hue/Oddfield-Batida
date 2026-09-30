#include "Library.h"

namespace batida
{

namespace
{
const char* folderName (PresetType type)
{
    switch (type)
    {
        case PresetType::Sound:   return "Sounds";
        case PresetType::Kit:     return "Kits";
        case PresetType::Pattern: return "Patterns";
        case PresetType::Set:     return "Sets";
    }
    return "";
}

// The factory archive, set by the plugin (and tests) at startup.
const void* archiveData = nullptr;
size_t archiveSize = 0;

// The archive's factory-version.txt: the version number, then an ID of the
// content ("8 65a822caa00e0180").
juce::String readArchiveStamp()
{
    if (archiveData == nullptr)
        return {};
    juce::ZipFile zip (new juce::MemoryInputStream (archiveData, archiveSize, false), true);
    if (const auto* entry = zip.getEntry ("factory-version.txt"))
        if (std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (*entry)); in != nullptr)
            return in->readEntireStreamAsString().trim();
    return {};
}

int readArchiveVersion()
{
    return readArchiveStamp().getIntValue();
}
} // namespace

Library::Library() : Library (defaultRoot(), defaultSettingsFile()) {}

Library::Library (const juce::File& r, const juce::File& settings) : root (r), settingsFile (settings)
{
    loadSettings();
    loadFavourites();
    loadDecisions();
}

// Checks the User folder every 2 s and refreshes the index when it changed.
class Library::Watcher final : public juce::Thread
{
public:
    Watcher (Library& l) : juce::Thread ("Batida library watcher"), library (l), folder (l.root.getChildFile ("User"))
    {
        last = folderSignature (folder);
        startThread (juce::Thread::Priority::low);
    }

    ~Watcher() override { stopThread (3000); }

    void run() override
    {
        while (! threadShouldExit())
        {
            wait (2000);
            if (threadShouldExit())
                return;
            const auto now = folderSignature (folder);
            if (now == last)
                continue;
            last = now;
            juce::WeakReference<Library> safe (&library);
            juce::MessageManager::callAsync ([safe]
            {
                if (auto* self = safe.get())
                    self->refresh();
            });
        }
    }

private:
    Library& library;
    const juce::File folder;
    juce::int64 last = 0;
};

Library::~Library()
{
    watcher.reset();
    pool.removeAllJobs (true, 5000);
}

void Library::startWatching()
{
    if (watchers++ == 0)
        watcher = std::make_unique<Watcher> (*this);
}

void Library::stopWatching()
{
    if (watchers > 0 && --watchers == 0)
        watcher.reset();
}

juce::int64 Library::folderSignature (const juce::File& folder)
{
    juce::uint64 h = 17;
    if (folder.isDirectory())
        for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*.batida-*", juce::File::findFiles))
        {
            h = h * 31 + (juce::uint64) entry.getFile().getFullPathName().hashCode64();
            h = h * 31 + (juce::uint64) entry.getFileSize();
            h = h * 31 + (juce::uint64) entry.getModificationTime().toMilliseconds();
        }
    return (juce::int64) h;
}

namespace
{
// Until 0.8.4 the publisher was called Negative Space. The first time the
// renamed Batida looks for its folders, it moves the old ones (the library
// and the settings) to the new name, unless the new ones already exist.
void moveFromOldPublisherName()
{
    static const bool moved = []
    {
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        for (const auto& [from, to] : { std::pair { "Music/Negative Space/Batida", "Music/Oddfield/Batida" },
                                        std::pair { "Library/Application Support/Negative Space/Batida",
                                                    "Library/Application Support/Oddfield/Batida" } })
        {
            const auto oldDir = home.getChildFile (from), newDir = home.getChildFile (to);
            if (oldDir.isDirectory() && ! newDir.exists())
            {
                newDir.getParentDirectory().createDirectory();
                if (oldDir.moveFileTo (newDir))
                {
                    const auto oldParent = oldDir.getParentDirectory();
                    if (oldParent.getNumberOfChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles) == 0)
                        oldParent.deleteRecursively(); // only the empty "Negative Space" folder (maybe a .DS_Store)
                }
            }
        }
        return true;
    }();
    juce::ignoreUnused (moved);
}
} // namespace

juce::File Library::defaultSettingsFile()
{
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("BATIDA_LIBRARY", {}); env.isNotEmpty())
        return juce::File (env).getChildFile (".settings.xml");
    moveFromOldPublisherName();
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Application Support/Oddfield/Batida/Settings.xml");
}

juce::File Library::defaultRoot()
{
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("BATIDA_LIBRARY", {}); env.isNotEmpty())
        return juce::File (env);
    const auto standard = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Oddfield/Batida");
    if (const auto xml = juce::XmlDocument::parse (defaultSettingsFile()))
        if (const auto path = xml->getStringAttribute ("library"); juce::File::isAbsolutePath (path))
        {
            // A setting that still names the old standard folder follows the move.
            const auto old = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Negative Space/Batida");
            return juce::File (path) == old && ! old.exists() ? standard : juce::File (path);
        }
    return standard;
}

juce::File Library::folderFor (PresetType type, bool factory) const
{
    return root.getChildFile (factory ? "Factory" : "User").getChildFile (folderName (type));
}

juce::String Library::categoryFolder (const juce::String& category)
{
    if (category.isEmpty())
        return "Other";
    if (category == "fx")
        return "FX";
    return category.substring (0, 1).toUpperCase() + category.substring (1);
}

juce::File Library::userFileFor (PresetType type, const PresetInfo& info) const
{
    auto folder = folderFor (type, false);
    if (type == PresetType::Sound)
        folder = folder.getChildFile (categoryFolder (info.category));
    return folder.getChildFile (safeFileName (info.name) + extensionFor (type));
}

bool Library::isFactory (const juce::File& file) const
{
    return file.isAChildOf (root.getChildFile ("Factory"));
}

void Library::setFactoryArchive (const void* data, size_t size)
{
    archiveData = data;
    archiveSize = size;
}

int Library::archiveVersion()
{
    return readArchiveVersion();
}

bool Library::installFactory()
{
    // Installed when the archive is newer, or the same version with other
    // content (a factory rebuilt without a new number); never over a newer one.
    const auto archive = readArchiveStamp();
    const auto version = archive.getIntValue();
    const auto factory = root.getChildFile ("Factory");
    const auto stamp = factory.getChildFile (".version");
    const auto installed = stamp.loadFileAsString().trim();
    if (version <= 0 || installed.getIntValue() > version || installed == archive)
        return false;

    // Batida's own folder: replaced whole, so files a newer factory dropped go too.
    factory.deleteRecursively();
    factory.createDirectory();
    juce::ZipFile zip (new juce::MemoryInputStream (archiveData, archiveSize, false), true);
    const auto result = zip.uncompressTo (factory, true);
    factory.getChildFile ("factory-version.txt").deleteFile();
    if (result.wasOk())
        stamp.replaceWithText (archive);
    return true;
}

void Library::prepare()
{
    if (prepared)
        return;
    prepared = true;
    root.getChildFile ("User").createDirectory();
    installFactory();
    refresh();
}

std::vector<LibraryEntry> Library::scanFolder (const juce::File& root)
{
    std::vector<LibraryEntry> found;
    if (! root.isDirectory())
        return found;
    const auto factory = root.getChildFile ("Factory");
    const auto review = root.getChildFile ("Review");
    for (const auto& entry : juce::RangedDirectoryIterator (root, true, "*.batida-*", juce::File::findFiles))
    {
        const auto& f = entry.getFile();
        const auto type = presetTypeOf (f);
        const auto inReview = f.isAChildOf (review);
        if (! type || (inReview && ! kReviewBuild))
            continue;
        if (auto info = readInfo (f))
            found.push_back ({ f, *type, f.isAChildOf (factory), inReview, std::move (*info) });
    }
    return found;
}

void Library::scanNow()
{
    loadDecisions();
    entries = scanFolder (root);
    ++scanGeneration;
    ++version;
    sendChangeMessage();
}

void Library::refresh()
{
    const auto generation = ++scanGeneration;
    scanning = true;
    juce::WeakReference<Library> safe (this);
    const auto folder = root;
    pool.addJob ([safe, folder, generation]
    {
        auto found = scanFolder (folder);
        juce::MessageManager::callAsync ([safe, generation, found = std::move (found)]() mutable
        {
            auto* self = safe.get();
            if (self == nullptr || generation != self->scanGeneration)
                return; // a newer scan is on its way
            self->entries = std::move (found);
            self->loadDecisions();
            self->scanning = false;
            ++self->version;
            self->sendChangeMessage();
        });
    });
}

const LibraryEntry* Library::entryFor (const juce::File& file) const
{
    for (const auto& e : entries)
        if (e.file == file)
            return &e;
    return nullptr;
}

std::vector<LibraryEntry> Library::filtered (PresetType type, const LibraryFilter& filter) const
{
    juce::StringArray words;
    words.addTokens (filter.search.toLowerCase(), " ", "\"");
    words.removeEmptyStrings();

    std::vector<LibraryEntry> out;
    for (const auto& e : entries)
    {
        if (e.type != type)
            continue;
        using S = LibraryFilter::Source;
        if ((filter.source == S::Review) != e.review)
            continue; // candidates show only under Review, and only there
        if ((filter.source == S::Factory && ! e.factory) || (filter.source == S::User && e.factory)
            || (filter.source == S::Favourites && ! isFavourite (e.file)))
            continue;
        if (type == PresetType::Sound && filter.category.isNotEmpty() && e.info.category != filter.category)
            continue;

        bool ok = true;
        for (const auto& t : filter.tags)
            ok = ok && e.info.tags.contains (t, true);
        const auto haystack = (e.info.name + " " + e.info.tags.joinIntoString (" ") + " " + e.info.category + " "
                               + e.info.author).toLowerCase();
        for (const auto& w : words)
            ok = ok && haystack.contains (w);
        if (ok)
            out.push_back (e);
    }

    // Sounds by category, then name; everything else by name. Factory first on a tie.
    auto rank = [] (const juce::String& c) { const auto i = soundCategories().indexOf (c); return i < 0 ? 99 : i; };
    std::stable_sort (out.begin(), out.end(), [&] (const LibraryEntry& a, const LibraryEntry& b)
    {
        if (type == PresetType::Sound && rank (a.info.category) != rank (b.info.category))
            return rank (a.info.category) < rank (b.info.category);
        if (const auto c = a.info.name.compareNatural (b.info.name); c != 0)
            return c < 0;
        return a.factory && ! b.factory;
    });
    return out;
}

juce::String Library::keyFor (const juce::File& file) const
{
    return file.isAChildOf (root) ? file.getRelativePathFrom (root) : file.getFullPathName();
}

bool Library::isFavourite (const juce::File& file) const
{
    return favourites.contains (keyFor (file));
}

void Library::setFavourite (const juce::File& file, bool favourite)
{
    const auto key = keyFor (file);
    if (favourite == favourites.contains (key))
        return;
    if (favourite)
        favourites.add (key);
    else
        favourites.removeString (key);
    saveFavourites();
    ++version;
    sendChangeMessage();
}

bool Library::updateInfo (const juce::File& file, const PresetInfo& info)
{
    if (isFactory (file))
        return false;
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("BATIDA"))
        return false;
    auto* i = xml->getChildByName ("INFO");
    if (i == nullptr)
        i = xml->createNewChildElement ("INFO");
    i->setAttribute ("name", info.name);
    i->setAttribute ("author", info.author);
    i->setAttribute ("category", info.category);
    i->setAttribute ("tags", info.tags.joinIntoString (","));
    if (! xml->writeTo (file, presetTextFormat()))
        return false;
    for (auto& e : entries)
        if (e.file == file)
            e.info = info;
    ++version;
    sendChangeMessage();
    return true;
}

// Settings ---------------------------------------------------------------------

void Library::loadSettings()
{
    const auto xml = juce::XmlDocument::parse (settingsFile);
    if (xml == nullptr)
        return;
    author = xml->getStringAttribute ("author");
    zoom = juce::jlimit (100, 150, xml->getIntAttribute ("zoom", 100));
    for (auto* e : xml->getChildWithTagNameIterator ("SAMPLE_FOLDER"))
        sampleFolders.addIfNotAlreadyThere (juce::File (e->getStringAttribute ("path")));
    for (auto* e : xml->getChildWithTagNameIterator ("RELINK_FOLDER"))
        relinkFolders.addIfNotAlreadyThere (juce::File (e->getStringAttribute ("path")));
}

void Library::saveSettings() const
{
    auto xml = juce::XmlDocument::parse (settingsFile);
    if (xml == nullptr)
        xml = std::make_unique<juce::XmlElement> ("SETTINGS");
    xml->setAttribute ("author", author);
    xml->setAttribute ("zoom", zoom);
    xml->deleteAllChildElementsWithTagName ("SAMPLE_FOLDER");
    xml->deleteAllChildElementsWithTagName ("RELINK_FOLDER");
    for (const auto& f : sampleFolders)
        xml->createNewChildElement ("SAMPLE_FOLDER")->setAttribute ("path", f.getFullPathName());
    for (const auto& f : relinkFolders)
        xml->createNewChildElement ("RELINK_FOLDER")->setAttribute ("path", f.getFullPathName());
    settingsFile.getParentDirectory().createDirectory();
    xml->writeTo (settingsFile);
}

void Library::loadFavourites()
{
    favourites.clear();
    if (const auto xml = juce::XmlDocument::parse (root.getChildFile ("Favourites.xml")))
        for (auto* e : xml->getChildWithTagNameIterator ("FAVOURITE"))
            favourites.addIfNotAlreadyThere (e->getStringAttribute ("path"));
}

void Library::saveFavourites() const
{
    juce::XmlElement xml ("FAVOURITES");
    for (const auto& f : favourites)
        xml.createNewChildElement ("FAVOURITE")->setAttribute ("path", f);
    root.createDirectory();
    xml.writeTo (root.getChildFile ("Favourites.xml"));
}

// Review --------------------------------------------------------------------------

void Library::loadDecisions()
{
    decisions.clear();
    if (const auto d = juce::JSON::parse (root.getChildFile ("Review/decisions.json")); d.isObject())
        decisions = d.getDynamicObject()->getProperties();
}

void Library::saveDecisions() const
{
    auto* o = new juce::DynamicObject();
    for (const auto& d : decisions)
        o->setProperty (d.name, d.value);
    root.getChildFile ("Review/decisions.json").replaceWithText (juce::JSON::toString (juce::var (o)));
}

Decision Library::getDecision (const juce::File& file) const
{
    if (! isReview (file))
        return Decision::None;
    const auto d = decisions[juce::Identifier (file.getRelativePathFrom (root.getChildFile ("Review")))].toString();
    return d == "keep" ? Decision::Keep : d == "reject" ? Decision::Reject : Decision::None;
}

void Library::setDecision (const juce::File& file, Decision decision)
{
    if (! isReview (file))
        return;
    loadDecisions(); // the factory tool may have rewritten it
    const juce::Identifier key (file.getRelativePathFrom (root.getChildFile ("Review")));
    if (decision == Decision::None)
        decisions.remove (key);
    else
        decisions.set (key, decision == Decision::Keep ? "keep" : "reject");
    saveDecisions();
    ++version;
    sendChangeMessage();
}

void Library::setAuthor (const juce::String& name)
{
    if (name == author)
        return;
    author = name;
    saveSettings();
}

void Library::setZoom (int percent)
{
    percent = percent >= 138 ? 150 : percent >= 113 ? 125 : 100;
    if (percent == zoom)
        return;
    zoom = percent;
    saveSettings();
}

void Library::addSampleFolder (const juce::File& folder)
{
    if (! folder.isDirectory() || sampleFolders.contains (folder))
        return;
    sampleFolders.add (folder);
    saveSettings();
    ++version;
    sendChangeMessage();
}

void Library::removeSampleFolder (const juce::File& folder)
{
    sampleFolders.removeFirstMatchingValue (folder);
    saveSettings();
    ++version;
    sendChangeMessage();
}

void Library::addRelinkFolder (const juce::File& folder)
{
    if (! folder.isDirectory())
        return;
    relinkFolders.removeFirstMatchingValue (folder);
    relinkFolders.insert (0, folder);
    while (relinkFolders.size() > 10)
        relinkFolders.removeLast();
    saveSettings();
}

juce::Array<juce::File> Library::searchFolders() const
{
    juce::Array<juce::File> folders (relinkFolders);
    folders.addIfNotAlreadyThere (root);
    for (const auto& f : sampleFolders)
        folders.addIfNotAlreadyThere (f);
    return folders;
}

} // namespace batida
