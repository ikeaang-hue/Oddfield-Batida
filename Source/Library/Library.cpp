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

// The factory's sounds: the default kit, each with a category and a character.
struct FactorySound { int voice; const char* category; const char* tags; };
const FactorySound kFactorySounds[] = {
    { 0, "kick", "warm" },      { 1, "perc", "digital" }, { 2, "snare", "organic" }, { 3, "snare", "organic" },
    { 4, "perc", "warm" },      { 5, "bass", "warm" },    { 6, "hat", "metallic" },  { 7, "hat", "metallic" },
};
} // namespace

Library::Library() : Library (defaultRoot(), defaultSettingsFile()) {}

Library::Library (const juce::File& r, const juce::File& settings) : root (r), settingsFile (settings)
{
    loadSettings();
    loadFavourites();
}

Library::~Library()
{
    pool.removeAllJobs (true, 5000);
}

juce::File Library::defaultSettingsFile()
{
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("BATIDA_LIBRARY", {}); env.isNotEmpty())
        return juce::File (env).getChildFile (".settings.xml");
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Application Support/Negative Space/Batida/Settings.xml");
}

juce::File Library::defaultRoot()
{
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("BATIDA_LIBRARY", {}); env.isNotEmpty())
        return juce::File (env);
    if (const auto xml = juce::XmlDocument::parse (defaultSettingsFile()))
        if (const auto path = xml->getStringAttribute ("library"); juce::File::isAbsolutePath (path))
            return juce::File (path);
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Negative Space/Batida");
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

bool Library::installFactory()
{
    const auto factory = root.getChildFile ("Factory");
    const auto stamp = factory.getChildFile (".version");
    if (stamp.loadFileAsString().trim().getIntValue() >= kFactoryVersion)
        return false;

    const auto kit = defaultKitPreset();
    bool ok = factory.createDirectory();

    for (const auto& fs : kFactorySounds)
    {
        SoundPreset s;
        s.info.name = kit.names[(size_t) fs.voice];
        s.info.author = "Negative Space";
        s.info.category = fs.category;
        s.info.tags.addTokens (fs.tags, ",", "");
        s.params = kit.voices[(size_t) fs.voice];
        ok = writeSound (folderFor (PresetType::Sound, true).getChildFile (categoryFolder (fs.category))
                             .getChildFile (s.info.name + extensionFor (PresetType::Sound)), s) && ok;
    }

    auto neutral = kit;
    neutral.info.tags = juce::StringArray { "warm", "organic" };
    ok = writeKit (folderFor (PresetType::Kit, true).getChildFile ("Neutral.batida-kit"), neutral) && ok;

    PatternPreset beat;
    beat.info = { "Breakbeat", "Negative Space", {}, { "organic" }, {} };
    beat.pattern = breakbeatPattern();
    ok = writePattern (folderFor (PresetType::Pattern, true).getChildFile ("Breakbeat.batida-pattern"), beat) && ok;

    SetPreset set;
    set.info = { "Neutral Breakbeat", "Negative Space", {}, { "warm", "organic" }, {} };
    set.kit = neutral;
    set.kit.globals = defaultKitParams().global;
    set.patterns.patterns[0] = breakbeatPattern();
    ok = writeSet (folderFor (PresetType::Set, true).getChildFile ("Neutral Breakbeat.batida-set"), set) && ok;

    if (ok)
        stamp.replaceWithText (juce::String (kFactoryVersion));
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
    for (const auto& entry : juce::RangedDirectoryIterator (root, true, "*.batida-*", juce::File::findFiles))
    {
        const auto& f = entry.getFile();
        const auto type = presetTypeOf (f);
        if (! type)
            continue;
        if (auto info = readInfo (f))
            found.push_back ({ f, *type, f.isAChildOf (factory), std::move (*info) });
    }
    return found;
}

void Library::scanNow()
{
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
    if (! xml->writeTo (file))
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
