#include "LibraryPage.h"

#include "Library/SampleLocator.h"

using namespace batida;

namespace
{
const juce::Colour kPanel (0xff2a2d31), kAccent (0xffc0632a), kBlue (0xff2b7bb9);
const char* kModeNames[] = { "Sounds", "Kits", "Patterns", "Sets", "Samples" };
const char* kSourceNames[] = { "All", "Factory", "User", "\xe2\x99\xa5 Favs" };
constexpr int kRowHeight = 22, kHeart = 24;

void styleLabel (juce::Label& l, float size = 13.0f, bool bold = false, juce::Colour c = juce::Colours::lightgrey)
{
    l.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    l.setColour (juce::Label::textColourId, c);
    l.setJustificationType (juce::Justification::topLeft);
}

void styleChip (juce::TextButton& b, const juce::String& text)
{
    b.setButtonText (text);
    b.setClickingTogglesState (false);
    b.setColour (juce::TextButton::buttonOnColourId, kBlue);
}

juce::String sizeText (juce::int64 bytes)
{
    return bytes >= 1024 * 1024 ? juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB"
                                : juce::String (juce::jmax ((juce::int64) 1, bytes / 1024)) + " KB";
}
} // namespace

LibraryPage::LibraryPage (BatidaProcessor& p, LibraryController& c) : proc (p), controller (c)
{
    for (int i = 0; i < 5; ++i)
    {
        auto& b = modeButtons[(size_t) i];
        b.setButtonText (kModeNames[i]);
        b.setColour (juce::TextButton::buttonOnColourId, kAccent);
        b.setComponentID (juce::String ("lib") + kModeNames[i]);
        b.onClick = [this, i] { setMode ((Mode) i); };
        addAndMakeVisible (b);
    }

    showLabel.setText ("Show", juce::dontSendNotification);
    styleLabel (showLabel, 12.0f, false, juce::Colours::grey);
    addAndMakeVisible (showLabel);
    for (int i = 0; i < 4; ++i)
    {
        auto& b = sourceButtons[(size_t) i];
        styleChip (b, juce::String::fromUTF8 (kSourceNames[i]));
        b.onClick = [this, i] { filter().source = (LibraryFilter::Source) i; rebuild(); };
        addAndMakeVisible (b);
    }

    styleChip (categoryButtons[0], "All");
    categoryButtons[0].onClick = [this] { filter().category = {}; rebuild(); };
    for (int i = 0; i < soundCategories().size(); ++i)
    {
        auto& b = categoryButtons[(size_t) i + 1];
        styleChip (b, Library::categoryFolder (soundCategories()[i]));
        b.onClick = [this, i]
        {
            const auto cat = soundCategories()[i];
            filter().category = filter().category == cat ? juce::String() : cat;
            rebuild();
        };
    }
    for (auto& b : categoryButtons)
        addAndMakeVisible (b);

    for (int i = 0; i < 5; ++i)
    {
        auto& b = tagButtons[(size_t) i];
        styleChip (b, characterTags()[i]);
        b.onClick = [this, i]
        {
            auto& tags = filter().tags;
            const auto t = characterTags()[i];
            if (tags.contains (t))
                tags.removeString (t);
            else
                tags.add (t);
            rebuild();
        };
        addAndMakeVisible (b);
    }

    search.setTextToShowWhenEmpty ("Search", juce::Colours::grey);
    search.setComponentID ("libSearch");
    search.onTextChange = [this] { filter().search = search.getText(); rebuild(); };
    search.onEscapeKey = [this] { search.clear(); filter().search = {}; rebuild(); };
    addAndMakeVisible (search);

    for (auto* l : { &countLabel, &hint })
    {
        styleLabel (*l, 12.0f, false, juce::Colours::grey);
        addAndMakeVisible (*l);
    }
    countLabel.setJustificationType (juce::Justification::centredLeft);

    list.setRowHeight (kRowHeight);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff232529));
    list.setOutlineThickness (0);
    list.setComponentID ("libList");
    addAndMakeVisible (list);

    previous.setTooltip ("Try the previous one");
    next.setTooltip ("Try the next one");
    previous.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    loadFile.onClick = [this]
    {
        if (mode != Mode::Samples)
        {
            controller.loadFromDisk (presetType());
            return;
        }
        chooser = std::make_unique<juce::FileChooser> ("Load a sample", folder, "*.wav;*.aif;*.aiff;*.flac");
        juce::Component::SafePointer<LibraryPage> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe != nullptr && fc.getResult().existsAsFile())
                                      safe->proc.loadSample (safe->controller.voice(), fc.getResult());
                              });
    };
    saveAs.onClick = [this] { controller.saveAs (presetType()); };
    init.onClick = [this] { controller.init (presetType()); };
    reveal.onClick = [this]
    {
        if (mode == Mode::Samples)
        {
            const auto r = list.getSelectedRow();
            (r >= 0 && r < (int) sampleRows.size() ? sampleRows[(size_t) r] : folder).revealToUser();
        }
        else if (const auto r = list.getSelectedRow(); r >= 0 && r < (int) rows.size())
            rows[(size_t) r].file.revealToUser();
        else
            proc.library().folderFor (presetType(), false).revealToUser();
    };
    for (juce::Button* b : { (juce::Button*) &previous, (juce::Button*) &next, (juce::Button*) &loadFile,
                             (juce::Button*) &saveAs, (juce::Button*) &init, (juce::Button*) &reveal })
        addAndMakeVisible (*b);

    styleLabel (detailName, 16.0f, true, juce::Colours::white);
    styleLabel (detailWhat, 12.0f, false, juce::Colours::grey);
    styleLabel (detailInfo, 12.0f);
    styleLabel (detailPath, 11.0f, false, juce::Colours::grey);
    for (auto* l : { &detailName, &detailWhat, &detailInfo, &detailPath })
        addAndMakeVisible (*l);

    // Editing a User file's category and tags.
    detailCategory.addItem ("No category", 1);
    for (int i = 0; i < soundCategories().size(); ++i)
        detailCategory.addItem (Library::categoryFolder (soundCategories()[i]), i + 2);
    detailCategory.onChange = [this]
    {
        const auto r = list.getSelectedRow();
        if (r < 0 || r >= (int) rows.size() || mode == Mode::Samples)
            return;
        auto info = rows[(size_t) r].info;
        info.category = detailCategory.getSelectedId() > 1 ? soundCategories()[detailCategory.getSelectedId() - 2] : juce::String();
        if (info.category != rows[(size_t) r].info.category)
            proc.library().updateInfo (rows[(size_t) r].file, info);
    };
    addChildComponent (detailCategory);
    for (int i = 0; i < 5; ++i)
    {
        auto& b = detailTags[(size_t) i];
        b.setButtonText (characterTags()[i]);
        b.setColour (juce::TextButton::buttonOnColourId, kBlue);
        b.onClick = [this, i]
        {
            const auto r = list.getSelectedRow();
            if (r < 0 || r >= (int) rows.size() || mode == Mode::Samples)
                return;
            auto info = rows[(size_t) r].info;
            const auto t = characterTags()[i];
            if (info.tags.contains (t))
                info.tags.removeString (t);
            else
                info.tags.add (t);
            proc.library().updateInfo (rows[(size_t) r].file, info);
        };
        addChildComponent (b);
    }

    // Samples: where to browse.
    rootBox.onChange = [this]
    {
        const auto id = rootBox.getSelectedId();
        if (id == 1)
            setFolder (proc.library().getRoot());
        else if (id >= 2 && id - 2 < proc.library().getSampleFolders().size())
            setFolder (proc.library().getSampleFolders()[id - 2]);
    };
    upButton.onClick = [this] { setFolder (folder.getParentDirectory()); };
    addFolderButton.onClick = [this] { addFolder(); };
    removeFolderButton.onClick = [this]
    {
        const auto id = rootBox.getSelectedId();
        if (id >= 2 && id - 2 < proc.library().getSampleFolders().size())
        {
            proc.library().removeSampleFolder (proc.library().getSampleFolders()[id - 2]);
            rebuildRoots();
            setFolder (proc.library().getRoot());
        }
    };
    for (auto* c : { (juce::Component*) &rootBox, (juce::Component*) &upButton, (juce::Component*) &addFolderButton,
                     (juce::Component*) &removeFolderButton })
        addChildComponent (c);

    proc.library().addChangeListener (this);
    folder = proc.library().getRoot();
    setMode (Mode::Sounds);
    startTimerHz (4);
}

LibraryPage::~LibraryPage()
{
    stopTimer();
    proc.library().removeChangeListener (this);
}

void LibraryPage::setMode (Mode m)
{
    mode = m;
    for (int i = 0; i < 5; ++i)
        modeButtons[(size_t) i].setToggleState (i == (int) m, juce::dontSendNotification);

    const auto samples = m == Mode::Samples;
    for (auto& b : categoryButtons)
        b.setVisible (m == Mode::Sounds);
    for (auto& b : tagButtons)
        b.setVisible (! samples);
    for (auto& b : sourceButtons)
        b.setVisible (! samples);
    showLabel.setVisible (! samples);
    search.setVisible (! samples);
    for (auto* c : { (juce::Component*) &rootBox, (juce::Component*) &upButton, (juce::Component*) &addFolderButton,
                     (juce::Component*) &removeFolderButton })
        c->setVisible (samples);
    saveAs.setVisible (! samples);
    init.setVisible (! samples);
    if (! samples)
        search.setText (filter().search, juce::dontSendNotification);
    else
        rebuildRoots();

    shownRow = -1;
    rebuild();
    resized();
}

void LibraryPage::visibilityChanged()
{
    if (isVisible())
    {
        proc.library().prepare();
        proc.library().refresh(); // pick up files changed in Finder
    }
}

void LibraryPage::rebuild()
{
    if (mode == Mode::Samples)
    {
        setFolder (folder);
        return;
    }
    juce::File keep; // the selection stays on the same file (tags edited, list re-read)
    if (const auto r = list.getSelectedRow(); r >= 0 && r < (int) rows.size())
        keep = rows[(size_t) r].file;
    rows = proc.library().shown (presetType());
    shownVersion = proc.library().getVersion();
    list.updateContent();
    updateFilterButtons();

    const auto n = (int) rows.size();
    const juce::String noun = juce::String (typeName (presetType())) + (n == 1 ? "" : "s");
    countLabel.setText (proc.library().isScanning() && n == 0 ? juce::String ("Reading the library...")
                                                              : juce::String (n) + " " + noun,
                        juce::dontSendNotification);

    // Show the one in use.
    int current = -1;
    for (int i = 0; i < n; ++i)
        if (rows[(size_t) i].file == keep)
            current = i;
    for (int i = 0; i < n && current < 0; ++i)
        if (isCurrent (i))
            current = i;
    selecting = true;
    if (current >= 0)
        list.selectRow (current);
    else
        list.deselectAllRows();
    selecting = false;
    list.repaint();
    updateDetails();
}

void LibraryPage::updateFilterButtons()
{
    if (mode == Mode::Samples)
        return;
    const auto& f = filter();
    for (int i = 0; i < 4; ++i)
        sourceButtons[(size_t) i].setToggleState ((int) f.source == i, juce::dontSendNotification);
    categoryButtons[0].setToggleState (f.category.isEmpty(), juce::dontSendNotification);
    for (int i = 0; i < soundCategories().size(); ++i)
        categoryButtons[(size_t) i + 1].setToggleState (f.category == soundCategories()[i], juce::dontSendNotification);
    for (int i = 0; i < 5; ++i)
        tagButtons[(size_t) i].setToggleState (f.tags.contains (characterTags()[i]), juce::dontSendNotification);
}

bool LibraryPage::isCurrent (int row) const
{
    if (mode == Mode::Samples)
        return row >= 0 && row < (int) sampleRows.size()
            && sampleRows[(size_t) row].getFullPathName() == proc.sampleSlot (controller.voice()).getPath();
    return row >= 0 && row < (int) rows.size() && rows[(size_t) row].file == controller.currentFile (presetType());
}

void LibraryPage::updateDetails()
{
    const auto r = list.getSelectedRow();
    for (auto& b : detailTags)
        b.setVisible (false);
    detailCategory.setVisible (false);

    auto target = [this]
    {
        switch (mode)
        {
            case Mode::Sounds:
            case Mode::Samples:  return "voice " + juce::String (controller.voice() + 1) + " (" + proc.getVoiceName (controller.voice()) + ")";
            case Mode::Kits:     return juce::String ("the whole kit (the patterns stay)");
            case Mode::Patterns: return "pattern " + juce::String (controller.pattern() + 1);
            case Mode::Sets:     return juce::String ("everything: kit, patterns and settings");
        }
        return juce::String();
    };
    hint.setText ("Click to try it: it loads into " + target() + ", live. Undo goes back to before you started browsing.",
                  juce::dontSendNotification);

    if (mode == Mode::Samples)
    {
        if (r >= 0 && r < (int) sampleRows.size())
        {
            const auto& f = sampleRows[(size_t) r];
            detailName.setText (f.getFileName(), juce::dontSendNotification);
            detailWhat.setText (f.isDirectory() ? "Folder" : "Sample, " + sizeText (f.getSize()), juce::dontSendNotification);
            detailPath.setText (f.getFullPathName(), juce::dontSendNotification);
        }
        else
        {
            detailName.setText (folder.getFileName(), juce::dontSendNotification);
            detailWhat.setText ("Folder", juce::dontSendNotification);
            detailPath.setText (folder.getFullPathName(), juce::dontSendNotification);
        }
        detailInfo.setText ("Drag a WAV, AIFF or FLAC from Finder onto a voice, or add your sample folders here.",
                            juce::dontSendNotification);
        return;
    }

    if (r < 0 || r >= (int) rows.size())
    {
        detailName.setText ({}, juce::dontSendNotification);
        detailWhat.setText ({}, juce::dontSendNotification);
        detailPath.setText ({}, juce::dontSendNotification);
        detailInfo.setText (rows.empty() ? juce::String ("Nothing here yet. Save one with Save..., or change the filters.")
                                         : juce::String ("Click one to try it."),
                            juce::dontSendNotification);
        return;
    }

    const auto& e = rows[(size_t) r];
    detailName.setText (e.info.name, juce::dontSendNotification);
    const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
    detailWhat.setText (juce::String (typeName (e.type)).toUpperCase() + dot + (e.factory ? "Factory" : "User")
                            + (proc.library().isFavourite (e.file) ? dot + juce::String::fromUTF8 ("\xe2\x99\xa5") : juce::String()),
                        juce::dontSendNotification);
    juce::StringArray lines;
    if (e.info.category.isNotEmpty())
        lines.add ("Category: " + Library::categoryFolder (e.info.category));
    if (! e.info.tags.isEmpty())
        lines.add ("Character: " + e.info.tags.joinIntoString (", "));
    if (e.info.author.isNotEmpty())
        lines.add ("By " + e.info.author);
    if (e.info.madeWith.isNotEmpty())
        lines.add ("Made with Batida " + e.info.madeWith);
    if (! e.factory)
        lines.add (juce::String ("Edit its character below."));
    detailInfo.setText (lines.joinIntoString ("\n"), juce::dontSendNotification);
    detailPath.setText (e.file.getRelativePathFrom (proc.library().getRoot()), juce::dontSendNotification);

    if (! e.factory)
    {
        for (int i = 0; i < 5; ++i)
        {
            detailTags[(size_t) i].setVisible (true);
            detailTags[(size_t) i].setToggleState (e.info.tags.contains (characterTags()[i]), juce::dontSendNotification);
        }
        if (e.type == PresetType::Sound)
        {
            detailCategory.setVisible (true);
            const auto id = soundCategories().indexOf (e.info.category) + 2;
            detailCategory.setSelectedId (id >= 2 ? id : 1, juce::dontSendNotification);
        }
    }
}

int LibraryPage::getNumRows()
{
    return mode == Mode::Samples ? (int) sampleRows.size() : (int) rows.size();
}

juce::Component* LibraryPage::refreshComponentForRow (int row, bool, juce::Component* existing)
{
    auto* r = dynamic_cast<Row*> (existing);
    if (r == nullptr)
    {
        delete existing;
        r = new Row (*this);
    }
    r->row = row;
    r->repaint();
    return r;
}

void LibraryPage::Row::paint (juce::Graphics& g)
{
    auto& p = page;
    if (row < 0 || row >= p.getNumRows())
        return;
    const auto selected = p.list.isRowSelected (row);
    const auto current = p.isCurrent (row);
    auto r = getLocalBounds();
    if (selected)
        g.fillAll (kBlue.withAlpha (0.35f));
    else if (row % 2 == 1)
        g.fillAll (juce::Colours::white.withAlpha (0.02f));

    if (p.mode == Mode::Samples)
    {
        const auto& f = p.sampleRows[(size_t) row];
        r.removeFromLeft (8);
        g.setFont (juce::FontOptions (13.0f, current ? juce::Font::bold : juce::Font::plain));
        g.setColour (current ? kAccent.brighter (0.3f) : juce::Colours::white.withAlpha (0.9f));
        if (f.isDirectory())
        {
            g.setColour (juce::Colours::lightgrey);
            g.drawText (juce::String::fromUTF8 ("\xe2\x96\xb8 ") + f.getFileName(), r, juce::Justification::centredLeft);
            return;
        }
        g.drawText (f.getFileName(), r.withTrimmedRight (70), juce::Justification::centredLeft);
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (sizeText (f.getSize()), r.removeFromRight (66), juce::Justification::centredRight);
        return;
    }

    const auto& e = p.rows[(size_t) row];
    const auto fav = p.proc.library().isFavourite (e.file);
    g.setFont (juce::FontOptions (14.0f));
    g.setColour (fav ? kAccent : juce::Colours::white.withAlpha (0.25f));
    g.drawText (juce::String::fromUTF8 (fav ? "\xe2\x99\xa5" : "\xe2\x99\xa1"), r.removeFromLeft (kHeart), juce::Justification::centred);

    auto badge = r.removeFromRight (58);
    g.setFont (juce::FontOptions (10.0f));
    g.setColour (e.factory ? juce::Colours::grey : kBlue.brighter (0.4f));
    g.drawText (e.factory ? "Factory" : "User", badge.withTrimmedRight (6), juce::Justification::centredRight);

    g.setFont (juce::FontOptions (13.0f, current ? juce::Font::bold : juce::Font::plain));
    g.setColour (current ? kAccent.brighter (0.3f) : juce::Colours::white.withAlpha (0.9f));
    g.drawText (e.info.name, r.removeFromLeft (220), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (juce::Colours::lightgrey);
    if (e.type == PresetType::Sound)
        g.drawText (Library::categoryFolder (e.info.category), r.removeFromLeft (70), juce::Justification::centredLeft);
    g.setColour (juce::Colours::grey);
    g.drawText (e.info.tags.joinIntoString (", "), r, juce::Justification::centredLeft);
}

void LibraryPage::clickRow (int row, int x)
{
    if (row < 0 || row >= getNumRows())
        return;
    if (mode != Mode::Samples && x < kHeart)
    {
        const auto& f = rows[(size_t) row].file;
        proc.library().setFavourite (f, ! proc.library().isFavourite (f));
        return;
    }
    if (mode == Mode::Samples && sampleRows[(size_t) row].isDirectory())
    {
        setFolder (sampleRows[(size_t) row]);
        return;
    }
    selecting = true;
    list.selectRow (row);
    selecting = false;
    tryRow (row);
}

void LibraryPage::selectedRowsChanged (int row)
{
    // Arrow keys: try the newly selected one (clicks are handled in clickRow).
    if (selecting || row < 0 || row == shownRow)
        return;
    if (mode == Mode::Samples && (row >= (int) sampleRows.size() || sampleRows[(size_t) row].isDirectory()))
        return;
    tryRow (row);
}

void LibraryPage::tryRow (int row)
{
    shownRow = row;
    if (mode == Mode::Samples)
        controller.trySample (sampleRows[(size_t) row]);
    else
        controller.tryFile (rows[(size_t) row].file);
    list.repaint();
    updateDetails();
}

void LibraryPage::step (int delta)
{
    const auto n = getNumRows();
    if (n == 0)
        return;
    auto r = list.getSelectedRow();
    for (int tries = 0; tries < n; ++tries)
    {
        r = r < 0 ? (delta > 0 ? 0 : n - 1) : ((r + delta) % n + n) % n;
        if (mode != Mode::Samples || ! sampleRows[(size_t) r].isDirectory())
            break;
    }
    if (mode == Mode::Samples && sampleRows[(size_t) r].isDirectory())
        return;
    selecting = true;
    list.selectRow (r);
    selecting = false;
    tryRow (r);
}

void LibraryPage::timerCallback()
{
    if (! isShowing())
        return;
    if (mode != Mode::Samples && proc.library().getVersion() != shownVersion)
        rebuild();
    if (proc.getOriginsVersion() != shownOrigins)
    {
        shownOrigins = proc.getOriginsVersion();
        list.repaint();
        updateDetails();
    }
}

// Samples -----------------------------------------------------------------------

void LibraryPage::rebuildRoots()
{
    rootBox.clear (juce::dontSendNotification);
    rootBox.addItem ("Library", 1);
    const auto folders = proc.library().getSampleFolders();
    for (int i = 0; i < folders.size(); ++i)
        rootBox.addItem (folders[i].getFileName() + "  (" + folders[i].getParentDirectory().getFullPathName() + ")", i + 2);
    int id = 1;
    for (int i = 0; i < folders.size(); ++i)
        if (folder == folders[i] || folder.isAChildOf (folders[i]))
            id = i + 2;
    rootBox.setSelectedId (id, juce::dontSendNotification);
    removeFolderButton.setEnabled (id >= 2);
}

void LibraryPage::setFolder (const juce::File& f)
{
    folder = f.isDirectory() ? f : proc.library().getRoot();
    sampleRows.clear();
    std::vector<juce::File> files;
    for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFilesAndDirectories))
    {
        const auto& file = entry.getFile();
        if (file.getFileName().startsWithChar ('.'))
            continue;
        if (entry.isDirectory())
            sampleRows.push_back (file);
        else if (isAudioFileName (file.getFullPathName()))
            files.push_back (file);
    }
    auto byName = [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; };
    std::sort (sampleRows.begin(), sampleRows.end(), byName);
    std::sort (files.begin(), files.end(), byName);
    sampleRows.insert (sampleRows.end(), files.begin(), files.end());

    list.updateContent();
    int current = -1;
    for (int i = 0; i < (int) sampleRows.size(); ++i)
        if (isCurrent (i))
            current = i;
    selecting = true;
    if (current >= 0)
        list.selectRow (current);
    else
        list.deselectAllRows();
    selecting = false;
    list.repaint();

    // The folder's place relative to its root, shown as a path.
    countLabel.setText (juce::String ((int) files.size()) + " sample" + (files.size() == 1 ? "" : "s"), juce::dontSendNotification);
    const auto root = rootBox.getSelectedId() >= 2 ? proc.library().getSampleFolders()[rootBox.getSelectedId() - 2]
                                                  : proc.library().getRoot();
    upButton.setEnabled (folder != root && folder.isAChildOf (root));
    updateDetails();
}

void LibraryPage::addFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Add a sample folder to browse",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    juce::Component::SafePointer<LibraryPage> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || ! fc.getResult().isDirectory())
                                  return;
                              safe->proc.library().addSampleFolder (fc.getResult());
                              safe->folder = fc.getResult();
                              safe->rebuildRoots();
                              safe->setFolder (fc.getResult());
                          });
}

// Layout -------------------------------------------------------------------------

void LibraryPage::paint (juce::Graphics& g)
{
    g.fillAll (kPanel);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRect (getLocalBounds().removeFromRight (250));
}

void LibraryPage::resized()
{
    auto r = getLocalBounds().reduced (10);

    auto left = r.removeFromLeft (132);
    r.removeFromLeft (10);
    for (auto& b : modeButtons)
    {
        b.setBounds (left.removeFromTop (30).reduced (0, 2));
    }
    left.removeFromTop (14);
    showLabel.setBounds (left.removeFromTop (18));
    for (int row = 0; row < 2; ++row)
    {
        auto line = left.removeFromTop (26);
        sourceButtons[(size_t) row * 2].setBounds (line.removeFromLeft (line.getWidth() / 2).reduced (1, 1));
        sourceButtons[(size_t) row * 2 + 1].setBounds (line.reduced (1, 1));
    }

    auto right = r.removeFromRight (230);
    r.removeFromRight (20);
    detailName.setBounds (right.removeFromTop (24));
    detailWhat.setBounds (right.removeFromTop (18));
    right.removeFromTop (6);
    detailInfo.setBounds (right.removeFromTop (92));
    auto tagRow = right.removeFromTop (48);
    for (int i = 0; i < 5; ++i)
    {
        auto cell = i < 3 ? tagRow.withHeight (22).withTrimmedLeft (i * 77).withWidth (74)
                          : tagRow.withTrimmedTop (25).withHeight (22).withTrimmedLeft ((i - 3) * 77).withWidth (74);
        detailTags[(size_t) i].setBounds (cell);
    }
    right.removeFromTop (4);
    detailCategory.setBounds (right.removeFromTop (24).withWidth (150));
    right.removeFromTop (8);
    reveal.setBounds (right.removeFromBottom (26).withWidth (120));
    right.removeFromBottom (4);
    detailPath.setBounds (right.removeFromBottom (42));
    right.removeFromBottom (6);
    hint.setBounds (right.removeFromBottom (46));

    // Middle: filters, the list, the actions.
    auto top = r.removeFromTop (28);
    if (mode == Mode::Samples)
    {
        rootBox.setBounds (top.removeFromLeft (250).reduced (0, 2));
        top.removeFromLeft (6);
        upButton.setBounds (top.removeFromLeft (44).reduced (0, 2));
        top.removeFromLeft (4);
        addFolderButton.setBounds (top.removeFromLeft (100).reduced (0, 2));
        top.removeFromLeft (4);
        removeFolderButton.setBounds (top.removeFromLeft (70).reduced (0, 2));
    }
    else
    {
        search.setBounds (top.removeFromLeft (220).reduced (0, 2));
    }
    top.removeFromLeft (10);
    countLabel.setBounds (top);

    if (mode == Mode::Sounds)
    {
        auto cats = r.removeFromTop (26);
        const auto w = cats.getWidth() / 8;
        for (auto& b : categoryButtons)
            b.setBounds (cats.removeFromLeft (w).reduced (1, 2));
    }
    if (mode != Mode::Samples)
    {
        auto tags = r.removeFromTop (26);
        const auto w = std::min (80, tags.getWidth() / 5);
        for (auto& b : tagButtons)
            b.setBounds (tags.removeFromLeft (w).reduced (1, 2));
    }
    r.removeFromTop (6);

    auto bottom = r.removeFromBottom (30);
    previous.setBounds (bottom.removeFromLeft (30).reduced (0, 2));
    next.setBounds (bottom.removeFromLeft (30).reduced (0, 2));
    bottom.removeFromLeft (8);
    init.setBounds (bottom.removeFromRight (50).reduced (0, 2));
    bottom.removeFromRight (4);
    saveAs.setBounds (bottom.removeFromRight (70).reduced (0, 2));
    bottom.removeFromRight (4);
    loadFile.setBounds (bottom.removeFromRight (90).reduced (0, 2));
    r.removeFromBottom (6);
    list.setBounds (r);
}
