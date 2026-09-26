#include "LibraryPage.h"

#include "Library/SampleLocator.h"

using namespace batida;

namespace
{
const char* kModeNames[] = { "SOUNDS", "KITS", "PATTERNS", "SETS", "SAMPLES" };
const char* kSourceNames[] = { "ALL", "FACTORY", "USER", "\xe2\x99\xa5", "REVIEW" };
constexpr int kRowHeight = 25, kHeart = 26;
constexpr int kNameW = 190, kCategoryW = 80, kTagsW = 110, kAuthorW = 130;

void styleLabel (juce::Label& l, float size = 10.5f, bool bold = false, juce::Colour c = theme::ink)
{
    l.setFont (bold ? theme::mono (size, theme::Weight::Bold) : theme::mono (size));
    l.setColour (juce::Label::textColourId, c);
    l.setJustificationType (juce::Justification::topLeft);
    l.setBorderSize ({});
}

void styleChip (juce::TextButton& b, const juce::String& text)
{
    b.setButtonText (text);
    b.setClickingTogglesState (false);
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
        b.setComponentID (juce::String ("lib") + juce::String (kModeNames[i]).toLowerCase().substring (0, 1).toUpperCase()
                          + juce::String (kModeNames[i]).toLowerCase().substring (1));
        b.onClick = [this, i] { setMode ((Mode) i); };
        addAndMakeVisible (b);
    }

    showLabel.setText ("", juce::dontSendNotification);
    addAndMakeVisible (showLabel);
    for (int i = 0; i < 5; ++i)
    {
        auto& b = sourceButtons[(size_t) i];
        styleChip (b, juce::String::fromUTF8 (kSourceNames[i]));
        b.onClick = [this, i] { filter().source = (LibraryFilter::Source) i; rebuild(); };
        addChildComponent (b);
    }

    // Review builds: keep or reject a preset factory candidate (K / R).
    keepButton.setTooltip ("Keep (K)");
    rejectButton.setTooltip ("Reject (R)");
    keepButton.onClick = [this] { decide (Decision::Keep); };
    rejectButton.onClick = [this] { decide (Decision::Reject); };
    addChildComponent (keepButton);
    addChildComponent (rejectButton);

    styleChip (categoryButtons[0], "all");
    categoryButtons[0].onClick = [this] { filter().category = {}; rebuild(); };
    for (int i = 0; i < soundCategories().size(); ++i)
    {
        auto& b = categoryButtons[(size_t) i + 1];
        styleChip (b, Library::categoryFolder (soundCategories()[i]).toLowerCase());
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

    search.setTextToShowWhenEmpty ("/  search name, tag, author", theme::faint);
    search.setFont (theme::mono (10.5f));
    search.setIndents (8, 6);
    search.setComponentID ("libSearch");
    search.onTextChange = [this] { filter().search = search.getText(); rebuild(); };
    search.onEscapeKey = [this] { search.clear(); filter().search = {}; rebuild(); };
    addAndMakeVisible (search);

    for (auto* l : { &countLabel, &hint })
    {
        styleLabel (*l, 9.5f, false, theme::muted);
        addAndMakeVisible (*l);
    }
    countLabel.setJustificationType (juce::Justification::centredLeft);

    list.setRowHeight (kRowHeight);
    list.setColour (juce::ListBox::backgroundColourId, theme::bg);
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

    styleLabel (detailName, 16.0f, true, theme::ink);
    detailName.setFont (theme::head (15.0f, true));
    styleLabel (detailWhat, 9.5f, false, theme::muted);
    styleLabel (detailInfo, 10.0f);
    styleLabel (detailPath, 9.5f, false, theme::muted);
    for (auto* l : { &detailName, &detailWhat, &detailInfo, &detailPath })
        addAndMakeVisible (*l);

    // A User (or Review) file's name: double-click to rename.
    detailName.onTextChange = [this]
    {
        const auto r = list.getSelectedRow();
        if (r < 0 || r >= (int) rows.size() || mode == Mode::Samples || rows[(size_t) r].factory)
            return;
        auto info = rows[(size_t) r].info;
        info.name = detailName.getText().trim();
        if (info.name.isNotEmpty() && info.name != rows[(size_t) r].info.name)
            proc.library().updateInfo (rows[(size_t) r].file, info);
        else
            detailName.setText (rows[(size_t) r].info.name, juce::dontSendNotification);
    };

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
    for (auto* comp : { (juce::Component*) &rootBox, (juce::Component*) &upButton, (juce::Component*) &addFolderButton,
                        (juce::Component*) &removeFolderButton })
        addChildComponent (comp);

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
    for (int i = 0; i < 5; ++i)
        sourceButtons[(size_t) i].setVisible (! samples && (i < 4 || proc.library().hasReview()));
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
    if (! isVisible())
        tries = 0;
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
    auto count = juce::String (n) + " " + noun;
    if (filter().source == LibraryFilter::Source::Review)
    {
        int kept = 0, rejected = 0;
        for (const auto& e : rows)
        {
            const auto d = proc.library().getDecision (e.file);
            kept += d == Decision::Keep ? 1 : 0;
            rejected += d == Decision::Reject ? 1 : 0;
        }
        count << juce::String::fromUTF8 (" \xc2\xb7 ") << kept << " kept" << juce::String::fromUTF8 (" \xc2\xb7 ")
              << rejected << " rejected";
    }
    countLabel.setText (proc.library().isScanning() && n == 0 ? juce::String ("Reading the library...") : count,
                        juce::dontSendNotification);
    sourceButtons[4].setVisible (mode != Mode::Samples && proc.library().hasReview());

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
    for (int i = 0; i < 5; ++i)
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
    keepButton.setVisible (false);
    rejectButton.setVisible (false);
    detailName.setEditable (false, false, false);

    auto target = [this]
    {
        switch (mode)
        {
            case Mode::Sounds:
            case Mode::Samples:  return juce::String (controller.voice() + 1).paddedLeft ('0', 2) + " " + proc.getVoiceName (controller.voice()).toUpperCase();
            case Mode::Kits:     return juce::String ("the kit");
            case Mode::Patterns: return "pattern " + juce::String (controller.pattern() + 1);
            case Mode::Sets:     return juce::String ("everything");
        }
        return juce::String();
    };
    // What a click loads into, and how many tries Undo would take back.
    hint.setText ("loads into " + target(), juce::dontSendNotification);

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
        detailInfo.setText ({}, juce::dontSendNotification);
        return;
    }

    if (r < 0 || r >= (int) rows.size())
    {
        detailName.setText ({}, juce::dontSendNotification);
        detailWhat.setText ({}, juce::dontSendNotification);
        detailPath.setText ({}, juce::dontSendNotification);
        detailInfo.setText (rows.empty() ? juce::String ("nothing here") : juce::String(), juce::dontSendNotification);
        return;
    }

    const auto& e = rows[(size_t) r];
    detailName.setText (e.info.name, juce::dontSendNotification);
    detailName.setEditable (false, ! e.factory, false);
    const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
    if (e.review)
    {
        const auto d = proc.library().getDecision (e.file);
        keepButton.setVisible (true);
        rejectButton.setVisible (true);
        keepButton.setToggleState (d == Decision::Keep, juce::dontSendNotification);
        rejectButton.setToggleState (d == Decision::Reject, juce::dontSendNotification);
    }
    detailWhat.setText (juce::String (typeName (e.type)).toUpperCase() + dot + (e.factory ? "Factory" : e.review ? "Review" : "User")
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
    using namespace theme;
    auto& p = page;
    if (row < 0 || row >= p.getNumRows())
        return;
    const auto selected = p.list.isRowSelected (row);
    const auto current = p.isCurrent (row);
    auto r = getLocalBounds();
    if (selected)
        g.fillAll (ink);
    g.setColour (selected ? ink : surface);
    g.fillRect (r.removeFromBottom (1));
    const auto fg = selected ? bg : ink;
    const auto dim = selected ? bg : muted;

    if (p.mode == Mode::Samples)
    {
        const auto& f = p.sampleRows[(size_t) row];
        r.removeFromLeft (10);
        if (f.isDirectory())
        {
            g.setColour (dim);
            g.setFont (mono (10.5f));
            g.drawText (juce::String::fromUTF8 ("\xe2\x96\xb8 ") + f.getFileName(), r, juce::Justification::centredLeft);
            return;
        }
        g.setColour (fg);
        g.setFont (mono (10.5f, current ? Weight::Bold : Weight::Medium));
        g.drawText (f.getFileName(), r.withTrimmedRight (70), juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (mono (9.5f));
        g.drawText (sizeText (f.getSize()), r.removeFromRight (70), juce::Justification::centredRight);
        return;
    }

    const auto& e = p.rows[(size_t) row];
    const auto fav = p.proc.library().isFavourite (e.file);
    r.removeFromLeft (4);
    g.setFont (mono (11.0f));
    g.setColour (fav ? (selected ? bg : lime) : (selected ? bg.withAlpha (0.4f) : faint));
    g.drawText (juce::String::fromUTF8 (fav ? "\xe2\x99\xa5" : "\xe2\x99\xa1"), r.removeFromLeft (kHeart - 4), juce::Justification::centredLeft);

    g.setColour (fg);
    g.setFont (mono (10.5f, current || selected ? Weight::Bold : Weight::Medium));
    g.drawText (e.info.name, r.removeFromLeft (kNameW), juce::Justification::centredLeft);
    g.setFont (mono (10.0f));
    g.setColour (dim);
    g.drawText (e.type == PresetType::Sound ? e.info.category : juce::String(), r.removeFromLeft (kCategoryW), juce::Justification::centredLeft);
    g.drawText (e.info.tags.joinIntoString (", "), r.removeFromLeft (kTagsW), juce::Justification::centredLeft);
    g.drawText (e.info.author, r.removeFromLeft (kAuthorW), juce::Justification::centredLeft);
    if (e.review)
    {
        const auto d = p.proc.library().getDecision (e.file);
        g.setFont (mono (10.0f, d == Decision::Keep ? Weight::Bold : Weight::Regular));
        g.setColour (d == Decision::Keep ? fg : d == Decision::Reject ? (selected ? bg.withAlpha (0.5f) : faint) : dim);
        g.drawText (d == Decision::Keep ? "KEEP" : d == Decision::Reject ? "REJECT" : juce::String::fromUTF8 ("\xe2\x80\x94"),
                    r.withTrimmedRight (10), juce::Justification::centredRight);
        return;
    }
    g.drawText (e.factory ? "FACTORY" : "USER", r.withTrimmedRight (10), juce::Justification::centredRight);
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
    ++tries;
    repaint (detailBounds);
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

void LibraryPage::decide (Decision decision)
{
    const auto r = list.getSelectedRow();
    if (mode == Mode::Samples || r < 0 || r >= (int) rows.size() || ! rows[(size_t) r].review)
        return;
    const auto file = rows[(size_t) r].file;
    const auto now = proc.library().getDecision (file) == decision ? Decision::None : decision; // a second press clears it
    proc.library().setDecision (file, now);
    if (now != Decision::None && r + 1 < (int) rows.size())
        step (1); // on to the next, playing it
    else
    {
        list.repaint();
        updateDetails();
    }
}

bool LibraryPage::keyPressed (const juce::KeyPress& key)
{
    if (! keepButton.isVisible()) // (typing in the search field never gets here: the field takes its keys)
        return false;
    if (key.getModifiers().isAnyModifierKeyDown() && ! key.getModifiers().isShiftDown())
        return false;
    auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    if (c == 0)
        c = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getKeyCode()); // letter keys: the upper-case code
    if (c == 'k' || c == 'r')
    {
        decide (c == 'k' ? Decision::Keep : Decision::Reject);
        return true;
    }
    return false;
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
    using namespace theme;
    g.fillAll (bg);

    // The list: a frame and its column headings.
    const auto listFrame = list.getBounds().expanded (1).withTop (list.getY() - 25).withBottom (previous.getBottom() + 5);
    g.setColour (line);
    g.drawRect (listFrame, 1);
    g.fillRect (listFrame.getX(), list.getY() - 1, listFrame.getWidth(), 1);
    g.fillRect (listFrame.getX(), previous.getY() - 5, listFrame.getWidth(), 1);
    auto head = juce::Rectangle<int> (list.getX(), list.getY() - 24, list.getWidth(), 22);
    if (mode != Mode::Samples)
    {
        head.removeFromLeft (kHeart);
        drawLabel (g, "Name", head.removeFromLeft (kNameW));
        drawLabel (g, "Category", head.removeFromLeft (kCategoryW));
        drawLabel (g, "Character", head.removeFromLeft (kTagsW));
        drawLabel (g, "Author", head.removeFromLeft (kAuthorW));
        drawLabel (g, "Source", head.withTrimmedRight (10), muted, juce::Justification::centredRight);
    }
    else
        drawLabel (g, "File", head.withTrimmedLeft (10));

    if (mode == Mode::Sounds)
        drawLabel (g, "Category", categoryButtons[0].getBounds().translated (-70, 0).withWidth (66));
    if (mode != Mode::Samples)
        drawLabel (g, "Character", tagButtons[0].getBounds().translated (-74, 0).withWidth (70));

    // Details
    g.setColour (line);
    g.drawRect (detailBounds, 1);
    drawTag (g, detailBounds.reduced (8, 7).withHeight (kTagHeight), "Selected");
    if (tries > 0 && mode != Mode::Samples)
        drawTag (g, hint.getBounds().translated (0, -20).withHeight (kTagHeight),
                 juce::String (tries) + (tries == 1 ? " try" : " tries"), lime, bg);
}

void LibraryPage::resized()
{
    auto r = getLocalBounds();

    // Row 1: what to browse, where from, search.
    auto top = r.removeFromTop (24);
    for (auto& b : modeButtons)
    {
        const auto w = (int) juce::GlyphArrangement::getStringWidth (theme::mono (10.0f, theme::Weight::Bold), b.getButtonText()) + 16;
        b.setBounds (top.removeFromLeft (w));
        top.removeFromLeft (-1);
    }
    top.removeFromLeft (14);
    for (auto& b : sourceButtons)
    {
        const auto w = (int) juce::GlyphArrangement::getStringWidth (theme::mono (10.0f, theme::Weight::Bold), b.getButtonText()) + 16;
        b.setBounds (top.removeFromLeft (w));
        top.removeFromLeft (-1);
    }
    search.setBounds (top.removeFromRight (250));
    if (mode == Mode::Samples)
    {
        auto t = r.withHeight (24).withTrimmedLeft (modeButtons[4].getRight() + 14);
        rootBox.setBounds (t.removeFromLeft (250));
        t.removeFromLeft (6);
        upButton.setBounds (t.removeFromLeft (44));
        t.removeFromLeft (4);
        addFolderButton.setBounds (t.removeFromLeft (104));
        t.removeFromLeft (4);
        removeFolderButton.setBounds (t.removeFromLeft (70));
    }
    r.removeFromTop (10);

    // Row 2: category and character chips.
    auto chips = r.removeFromTop (22);
    if (mode == Mode::Sounds)
    {
        chips.removeFromLeft (70);
        for (auto& b : categoryButtons)
        {
            const auto w = (int) juce::GlyphArrangement::getStringWidth (theme::mono (10.0f), b.getButtonText()) + 14;
            b.setBounds (chips.removeFromLeft (w));
            chips.removeFromLeft (4);
        }
        chips.removeFromLeft (14);
    }
    if (mode != Mode::Samples)
    {
        chips.removeFromLeft (74);
        for (auto& b : tagButtons)
        {
            const auto w = (int) juce::GlyphArrangement::getStringWidth (theme::mono (10.0f), b.getButtonText()) + 14;
            b.setBounds (chips.removeFromLeft (w));
            chips.removeFromLeft (4);
        }
    }
    r.removeFromTop (10);

    // Details on the right.
    detailBounds = r.removeFromRight (300);
    r.removeFromRight (10);
    auto d = detailBounds.reduced (10, 8).withTrimmedTop (theme::kTagHeight + 10);
    detailName.setBounds (d.removeFromTop (22));
    detailWhat.setBounds (d.removeFromTop (16));
    d.removeFromTop (6);
    detailInfo.setBounds (d.removeFromTop (78));
    auto tagRow = d.removeFromTop (46);
    for (int i = 0; i < 5; ++i)
    {
        auto cell = i < 3 ? tagRow.withHeight (20).withTrimmedLeft (i * 90).withWidth (86)
                          : tagRow.withTrimmedTop (24).withHeight (20).withTrimmedLeft ((i - 3) * 90).withWidth (86);
        detailTags[(size_t) i].setBounds (cell);
    }
    d.removeFromTop (4);
    auto categoryRow = d.removeFromTop (22);
    detailCategory.setBounds (categoryRow.removeFromLeft (150));
    categoryRow.removeFromLeft (6);
    rejectButton.setBounds (categoryRow.removeFromRight (62));
    categoryRow.removeFromRight (4);
    keepButton.setBounds (categoryRow.removeFromRight (52));
    reveal.setBounds (d.removeFromBottom (22));
    d.removeFromBottom (6);
    auto actions = d.removeFromBottom (22);
    const auto aw = (actions.getWidth() - 8) / 3;
    loadFile.setBounds (actions.removeFromLeft (aw));
    actions.removeFromLeft (4);
    saveAs.setBounds (actions.removeFromLeft (aw));
    actions.removeFromLeft (4);
    init.setBounds (actions);
    d.removeFromBottom (8);
    detailPath.setBounds (d.removeFromBottom (28));
    hint.setBounds (d.removeFromBottom (16));

    // The list, with its footer.
    auto listArea = r.reduced (1).withTrimmedTop (24);
    auto footer = listArea.removeFromBottom (28);
    countLabel.setBounds (footer.withTrimmedLeft (10).withTrimmedTop (8));
    next.setBounds (footer.removeFromRight (64).reduced (4));
    previous.setBounds (footer.removeFromRight (64).reduced (4));
    list.setBounds (listArea.withTrimmedBottom (1));
}
