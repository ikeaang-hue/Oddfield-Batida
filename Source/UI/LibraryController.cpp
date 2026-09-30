#include "LibraryController.h"

#include "Library/SampleLocator.h"
#include "Look/Theme.h"

using namespace batida;

namespace
{

juce::String capitalised (const juce::String& s)
{
    return s.substring (0, 1).toUpperCase() + s.substring (1);
}

void styleLabel (juce::Label& l, const juce::String& text, float size = 10.5f, bool bold = false)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (bold ? theme::mono (size, theme::Weight::Bold) : theme::mono (size));
    l.setColour (juce::Label::textColourId, theme::ink);
    l.setBorderSize ({});
}

void styleFieldLabel (juce::Label& l, const juce::String& text)
{
    styleLabel (l, text.toUpperCase(), 9.5f);
    l.setColour (juce::Label::textColourId, theme::muted);
    l.setJustificationType (juce::Justification::centredLeft);
}

juce::String tildePath (const juce::File& f)
{
    return f.getFullPathName().replace (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName(), "~");
}

void showMessage (const juce::String& title, const juce::String& text)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (title)
                                      .withMessage (text)
                                      .withButton ("OK"),
                                  nullptr);
}

void paintOverlay (juce::Graphics& g, juce::Rectangle<int> area, juce::Rectangle<int> box, const juce::String& tag, const juce::String& title)
{
    g.setColour (theme::bg.withAlpha (0.78f));
    g.fillRect (area);
    g.setColour (theme::bg);
    g.fillRect (box);
    g.setColour (theme::ink);
    g.drawRect (box, 1);
    const auto head = box.reduced (16, 14).withHeight (theme::kTagHeight);
    const auto w = theme::drawTag (g, head, tag);
    g.setColour (theme::ink);
    g.setFont (theme::head (11.0f, true));
    g.drawText (title.toUpperCase(), head.withTrimmedLeft (w + 10), juce::Justification::centredLeft, true);
}
} // namespace

// Save panel ------------------------------------------------------------------------

SavePanel::SavePanel (BatidaProcessor& p, PresetType t, int v, int pat) : proc (p), type (t), voice (v), pattern (pat)
{
    auto& lib = proc.library();
    PresetInfo info;
    juce::File origin;
    switch (type)
    {
        case PresetType::Sound:   info = proc.captureSound (voice).info; origin = proc.getOrigins().soundFiles[(size_t) voice]; break;
        case PresetType::Kit:     info = proc.captureKit().info; origin = proc.getOrigins().kitFile; break;
        case PresetType::Pattern: info = proc.capturePattern (pattern).info; origin = proc.getOrigins().patternFiles[(size_t) pattern]; break;
        case PresetType::Set:     info = proc.captureSet().info; origin = proc.getOrigins().setFile; break;
    }
    // Saving again: keep the category and tags the file already had.
    if (const auto before = origin.existsAsFile() ? readInfo (origin) : std::nullopt)
    {
        info.tags = before->tags;
        if (before->category.isNotEmpty())
            info.category = before->category;
    }
    if (info.author.isEmpty())
        info.author = lib.getAuthor();

    juce::String from;
    if (type == PresetType::Sound)
        from = "from " + juce::String (voice + 1).paddedLeft ('0', 2) + " " + proc.getVoiceName (voice);
    else if (type == PresetType::Pattern)
        from = "from pattern " + juce::String (pattern + 1);
    else if (type == PresetType::Set)
        from = "everything";
    tagText = "SAVE " + juce::String (typeName (type)).toUpperCase();
    titleText = from;
    title.setVisible (false);

    styleFieldLabel (nameLabel, "Name");
    styleFieldLabel (categoryLabel, "Category");
    styleFieldLabel (tagsLabel, "Character");
    styleFieldLabel (authorLabel, "Author");
    styleLabel (where, {}, 10.0f);
    styleLabel (message, {}, 10.0f);
    message.setColour (juce::Label::backgroundColourId, theme::ink);
    message.setColour (juce::Label::textColourId, theme::bg);
    message.setBorderSize ({ 0, 8, 0, 8 });
    for (auto* ed : { &name, &author })
    {
        ed->setFont (theme::mono (11.0f, theme::Weight::Medium));
        ed->setIndents (8, 6);
    }

    name.setText (info.name, false);
    name.setComponentID ("saveName");
    name.onTextChange = [this] { replacing = juce::File(); update(); };
    name.onReturnKey = [this] { save.triggerClick(); };
    name.onEscapeKey = [this] { if (onClose) onClose(); };
    author.setText (info.author, false);

    category.addItem ("None", 1);
    for (int i = 0; i < soundCategories().size(); ++i)
        category.addItem (Library::categoryFolder (soundCategories()[i]), i + 2);
    category.setSelectedId (soundCategories().indexOf (info.category) + 2, juce::dontSendNotification);
    if (category.getSelectedId() == 0)
        category.setSelectedId (1, juce::dontSendNotification);
    category.onChange = [this] { replacing = juce::File(); update(); };
    const auto isSound = type == PresetType::Sound;
    category.setVisible (false);
    categoryLabel.setVisible (isSound);
    for (int i = 0; i < soundCategories().size() && i < (int) categories.size(); ++i)
    {
        auto& b = categories[(size_t) i];
        b.setButtonText (Library::categoryFolder (soundCategories()[i]).toLowerCase());
        b.onClick = [this, i]
        {
            category.setSelectedId (category.getSelectedId() == i + 2 ? 1 : i + 2);
            for (int k = 0; k < (int) categories.size(); ++k)
                categories[(size_t) k].setToggleState (category.getSelectedId() == k + 2, juce::dontSendNotification);
        };
        b.setToggleState (category.getSelectedId() == i + 2, juce::dontSendNotification);
        b.setVisible (isSound);
        addChildComponent (b);
    }

    for (int i = 0; i < 5; ++i)
    {
        auto& b = tags[(size_t) i];
        b.setButtonText (characterTags()[i]);
        b.setClickingTogglesState (true);
        b.setToggleState (info.tags.contains (characterTags()[i]), juce::dontSendNotification);
        addAndMakeVisible (b);
    }

    bool anySample = false;
    for (int k = 0; k < kNumVoices; ++k)
        if ((type != PresetType::Sound || k == voice) && proc.sampleSlot (k).getStatus() == SampleSlot::Status::Loaded)
            anySample = true;
    collect.setVisible (type != PresetType::Pattern);
    collect.setEnabled (anySample);
    collect.setButtonText ("Collect samples");

    save.setColour (juce::TextButton::buttonColourId, theme::ink);
    save.setColour (juce::TextButton::textColourOffId, theme::bg);
    save.setComponentID ("saveButton");
    save.onClick = [this]
    {
        const auto file = proc.library().userFileFor (type, currentInfo());
        if (file.existsAsFile() && file != replacing)
        {
            replacing = file;
            update();
            return;
        }
        saveTo (file);
    };
    elsewhere.onClick = [this] { saveElsewhere(); };
    cancel.onClick = [this] { if (onClose) onClose(); };

    for (juce::Component* c : { (juce::Component*) &title, (juce::Component*) &nameLabel, (juce::Component*) &name,
                                (juce::Component*) &categoryLabel, (juce::Component*) &category, (juce::Component*) &tagsLabel,
                                (juce::Component*) &authorLabel, (juce::Component*) &author, (juce::Component*) &collect,
                                (juce::Component*) &where, (juce::Component*) &message, (juce::Component*) &save,
                                (juce::Component*) &elsewhere, (juce::Component*) &cancel })
        addAndMakeVisible (c);
    update();
}

PresetInfo SavePanel::currentInfo() const
{
    PresetInfo info;
    info.name = name.getText().trim().substring (0, 60);
    if (info.name.isEmpty())
        info.name = "Untitled";
    info.author = author.getText().trim();
    if (type == PresetType::Sound && category.getSelectedId() > 1)
        info.category = soundCategories()[category.getSelectedId() - 2];
    for (int i = 0; i < 5; ++i)
        if (tags[(size_t) i].getToggleState())
            info.tags.add (characterTags()[i]);
    return info;
}

void SavePanel::update()
{
    const auto file = proc.library().userFileFor (type, currentInfo());
    where.setText (tildePath (file), juce::dontSendNotification);
    const auto armed = replacing != juce::File() && replacing == file;
    save.setButtonText (armed ? "REPLACE" : "SAVE");
    message.setText (armed ? juce::String::fromUTF8 ("! \xe2\x80\x9c") + currentInfo().name + juce::String::fromUTF8 ("\xe2\x80\x9d already exists in User")
                           : juce::String(),
                     juce::dontSendNotification);
    message.setVisible (armed);
}

bool SavePanel::saveTo (const juce::File& file)
{
    auto info = currentInfo();
    proc.library().setAuthor (info.author);
    juce::String error;
    const auto collectSamples = collect.isEnabled() && collect.getToggleState();
    bool ok = false;
    switch (type)
    {
        case PresetType::Sound:   ok = proc.saveSound (voice, file, info, collectSamples, &error); break;
        case PresetType::Kit:     ok = proc.saveKit (file, info, collectSamples, &error); break;
        case PresetType::Pattern: ok = proc.savePattern (pattern, file, info, &error); break;
        case PresetType::Set:     ok = proc.saveSet (file, info, collectSamples, &error); break;
    }
    if (! ok)
    {
        message.setText ("! couldn't save: " + error, juce::dontSendNotification);
        message.setVisible (true);
        return false;
    }
    if (onClose)
        onClose();
    return true;
}

void SavePanel::saveElsewhere()
{
    const auto ext = juce::String (extensionFor (type));
    const auto suggested = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                               .getChildFile (safeFileName (currentInfo().name) + ext);
    chooser = std::make_unique<juce::FileChooser> ("Save the " + juce::String (typeName (type)) + " as", suggested, "*" + ext);
    juce::Component::SafePointer<SavePanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe, ext] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (safe == nullptr || file == juce::File())
                                  return;
                              if (! file.hasFileExtension (ext))
                                  file = file.withFileExtension (ext);
                              safe->saveTo (file);
                          });
}

void SavePanel::paint (juce::Graphics& g)
{
    paintOverlay (g, getLocalBounds(), box, tagText, titleText);
    g.setColour (theme::muted);
    g.setFont (theme::mono (9.5f));
    g.drawText ("SAVES TO", where.getBounds().withX (box.getX() + 16).withWidth (80), juce::Justification::centredLeft);
}

void SavePanel::resized()
{
    box = getLocalBounds().withSizeKeepingCentre (600, category.isVisible() || categoryLabel.isVisible() ? 330 : 300);
    auto r = box.reduced (16, 14);
    r.removeFromTop (theme::kTagHeight + 16);
    auto row = [&] (juce::Label& l, int h = 26)
    {
        auto a = r.removeFromTop (h);
        l.setBounds (a.removeFromLeft (88));
        r.removeFromTop (8);
        return a;
    };
    name.setBounds (row (nameLabel));
    if (categoryLabel.isVisible())
    {
        auto c = row (categoryLabel, 22);
        for (auto& b : categories)
        {
            const auto w = (int) juce::GlyphArrangement::getStringWidth (theme::mono (10.0f), b.getButtonText()) + 14;
            b.setBounds (c.removeFromLeft (w));
            c.removeFromLeft (4);
        }
    }
    auto t = row (tagsLabel, 22);
    for (auto& b : tags)
    {
        const auto w = (int) juce::GlyphArrangement::getStringWidth (theme::mono (10.0f), b.getButtonText()) + 14;
        b.setBounds (t.removeFromLeft (w));
        t.removeFromLeft (4);
    }
    author.setBounds (row (authorLabel).withWidth (300));
    if (collect.isVisible())
    {
        auto c = r.removeFromTop (22).withTrimmedLeft (88);
        collect.setBounds (c.withWidth ((int) juce::GlyphArrangement::getStringWidth (theme::mono (9.5f, theme::Weight::Bold), "[COLLECT SAMPLES]") + 14));
        r.removeFromTop (8);
    }
    where.setBounds (r.removeFromTop (16).withTrimmedLeft (88));
    r.removeFromTop (8);
    message.setBounds (r.removeFromTop (22));
    auto buttons = r.removeFromBottom (24);
    save.setBounds (buttons.removeFromLeft (84));
    buttons.removeFromLeft (6);
    elsewhere.setBounds (buttons.removeFromLeft (150));
    buttons.removeFromLeft (6);
    cancel.setBounds (buttons.removeFromLeft (80));
}

// Relink panel ---------------------------------------------------------------------

RelinkPanel::RelinkPanel (BatidaProcessor& p) : proc (p)
{
    title.setVisible (false);
    styleLabel (status, {}, 9.5f);
    status.setColour (juce::Label::textColourId, theme::muted);
    status.setJustificationType (juce::Justification::topLeft);
    searchButton.onClick = [this] { searchFolder(); };
    againButton.onClick = [this]
    {
        const auto n = proc.autoRelink();
        status.setText (n > 0 ? "Found " + juce::String (n) + "." : "Nothing new found there.", juce::dontSendNotification);
        rebuild();
    };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    for (juce::Component* c : { (juce::Component*) &title, (juce::Component*) &status, (juce::Component*) &searchButton,
                                (juce::Component*) &againButton, (juce::Component*) &closeButton })
        addAndMakeVisible (c);
    rebuild();
    startTimerHz (5);
}

RelinkPanel::~RelinkPanel()
{
    stopTimer();
    if (search != nullptr)
        search->stop = true;
}

void RelinkPanel::rebuild()
{
    rowLabels.clear();
    rowButtons.clear();
    shownMissing = proc.numMissingSamples();
    for (int v = 0; v < kNumVoices; ++v)
    {
        if (proc.sampleSlot (v).getStatus() != SampleSlot::Status::Missing)
            continue;
        const juce::File f (proc.sampleSlot (v).getPath());
        auto* l = rowLabels.add (new juce::Label());
        styleLabel (*l, juce::String (v + 1).paddedLeft ('0', 2) + " " + proc.getVoiceName (v).toUpperCase(), 10.5f, true);
        auto* fileLabel = rowLabels.add (new juce::Label());
        styleLabel (*fileLabel, f.getFileName(), 10.5f);
        auto* wasLabel = rowLabels.add (new juce::Label());
        styleLabel (*wasLabel, tildePath (f), 10.0f);
        wasLabel->setColour (juce::Label::textColourId, theme::muted);
        wasLabel->setMinimumHorizontalScale (0.6f);
        addAndMakeVisible (fileLabel);
        addAndMakeVisible (wasLabel);
        auto* b = rowButtons.add (new juce::TextButton (juce::String::fromUTF8 ("LOCATE\xe2\x80\xa6")));
        b->onClick = [this, v] { locate (v); };
        addAndMakeVisible (l);
        addAndMakeVisible (b);
    }
    if (shownMissing == 0)
        status.setText ("all samples found", juce::dontSendNotification);
    resized();
    repaint();
}

void RelinkPanel::timerCallback()
{
    if (search != nullptr && search->done)
    {
        // Only slots still missing the same file: a swap or a load during the
        // search must not receive another slot's result.
        std::vector<std::pair<int, juce::File>> stillWanted;
        for (const auto& [v, f] : search->found)
            if (proc.sampleSlot (v).getStatus() == SampleSlot::Status::Missing
                && proc.sampleSlot (v).getPath() == search->wantedPaths[(size_t) v])
                stillWanted.push_back ({ v, f });
        const auto found = proc.relinkFound (stillWanted);
        status.setText ("Found " + juce::String (found) + " of " + juce::String (shownMissing) + " in that folder.",
                        juce::dontSendNotification);
        search = nullptr;
        searchButton.setEnabled (true);
    }
    if (proc.numMissingSamples() != shownMissing)
        rebuild();
}

void RelinkPanel::searchFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Search a folder (and its subfolders) for the missing samples",
                                                   juce::File::getSpecialLocation (juce::File::userHomeDirectory));
    juce::Component::SafePointer<RelinkPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();
                              if (safe == nullptr || ! folder.isDirectory())
                                  return;
                              struct Want { int voice; juce::String name; juce::int64 bytes; };
                              std::vector<Want> wanted;
                              for (int v = 0; v < kNumVoices; ++v)
                                  if (safe->proc.sampleSlot (v).getStatus() == SampleSlot::Status::Missing)
                                      wanted.push_back ({ v, juce::File (safe->proc.sampleSlot (v).getPath()).getFileName(),
                                                          safe->proc.missingSampleBytes (v) });
                              auto s = std::make_shared<Search>();
                              for (const auto& w : wanted)
                                  s->wantedPaths[(size_t) w.voice] = safe->proc.sampleSlot (w.voice).getPath();
                              safe->search = s;
                              safe->searchButton.setEnabled (false);
                              safe->status.setText ("Searching " + folder.getFullPathName() + "...", juce::dontSendNotification);
                              juce::Thread::launch ([s, wanted, folder]
                              {
                                  for (const auto& w : wanted)
                                  {
                                      const auto f = findSample (w.name, w.bytes, { folder }, 500000, [s] { return s->stop.load(); });
                                      if (f.existsAsFile())
                                          s->found.push_back ({ w.voice, f });
                                      if (s->stop)
                                          break;
                                  }
                                  s->done = true;
                              });
                          });
}

void RelinkPanel::locate (int voice)
{
    const juce::File was (proc.sampleSlot (voice).getPath());
    chooser = std::make_unique<juce::FileChooser> ("Where is " + was.getFileName() + "?",
                                                   was.getParentDirectory().exists() ? was.getParentDirectory()
                                                                                     : juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac");
    juce::Component::SafePointer<RelinkPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, voice] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr && fc.getResult().existsAsFile())
                              {
                                  safe->proc.relinkSample (voice, fc.getResult());
                                  // The rest may be in the same folder.
                                  const auto more = safe->proc.autoRelink();
                                  if (more > 0)
                                      safe->status.setText ("Also found " + juce::String (more) + " more there.", juce::dontSendNotification);
                                  safe->rebuild();
                              }
                          });
}

void RelinkPanel::paint (juce::Graphics& g)
{
    paintOverlay (g, getLocalBounds(), box, "RELINK",
                  juce::String (shownMissing) + " sample" + (shownMissing == 1 ? "" : "s") + " missing");
    const auto top = box.getY() + 14 + theme::kTagHeight + 14;
    const auto x = box.getX() + 16;
    theme::drawLabel (g, "Slot", { x, top, 100, 14 });
    theme::drawLabel (g, "File", { x + 110, top, 160, 14 });
    theme::drawLabel (g, "Last seen", { x + 280, top, 200, 14 });
    g.setColour (theme::line);
    g.fillRect (x, top + 18, box.getWidth() - 32, 1);
}

void RelinkPanel::resized()
{
    const auto rows = std::max (1, rowButtons.size());
    box = getLocalBounds().withSizeKeepingCentre (760, 170 + 32 * rows);
    auto r = box.reduced (16, 14);
    r.removeFromTop (theme::kTagHeight + 14 + 24);
    for (int i = 0; i < rowButtons.size(); ++i)
    {
        auto row = r.removeFromTop (30);
        rowButtons[i]->setBounds (row.removeFromRight (84).withSizeKeepingCentre (84, 22));
        rowLabels[i * 3]->setBounds (row.removeFromLeft (110));
        rowLabels[i * 3 + 1]->setBounds (row.removeFromLeft (170));
        rowLabels[i * 3 + 2]->setBounds (row.withTrimmedRight (10));
        r.removeFromTop (2);
    }
    auto buttons = r.removeFromBottom (24);
    searchButton.setBounds (buttons.removeFromLeft (160));
    buttons.removeFromLeft (6);
    againButton.setBounds (buttons.removeFromLeft (110));
    buttons.removeFromLeft (6);
    closeButton.setBounds (buttons.removeFromLeft (70));
    status.setBounds (r.withTrimmedTop (8).removeFromTop (16));
}

// Controller ---------------------------------------------------------------------

LibraryController::LibraryController (BatidaProcessor& p, juce::Component& overlayParent) : proc (p), parent (overlayParent)
{
    proc.library().prepare();
}

LibraryController::~LibraryController() = default;

juce::String LibraryController::currentName (PresetType type) const
{
    const auto& o = proc.getOrigins();
    switch (type)
    {
        case PresetType::Sound:   return proc.getVoiceName (voice());
        case PresetType::Kit:     return o.kitName;
        case PresetType::Pattern: return proc.getPatternName (pattern());
        case PresetType::Set:     return o.setName.isNotEmpty() ? o.setName : o.kitName;
    }
    return {};
}

juce::File LibraryController::currentFile (PresetType type) const
{
    const auto& o = proc.getOrigins();
    switch (type)
    {
        case PresetType::Sound:   return o.soundFiles[(size_t) voice()];
        case PresetType::Kit:     return o.kitFile;
        case PresetType::Pattern: return o.patternFiles[(size_t) pattern()];
        case PresetType::Set:     return o.setFile;
    }
    return {};
}

void LibraryController::step (PresetType type, int delta)
{
    auto& lib = proc.library();
    if (lib.getVersion() == 0)
        lib.scanNow(); // the first use: read it now rather than wait
    const auto list = lib.shown (type);
    if (list.empty())
        return;
    const auto now = currentFile (type);
    int index = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].file == now)
            index = (int) i;
    const auto n = (int) list.size();
    index = index < 0 ? (delta > 0 ? 0 : n - 1) : ((index + delta) % n + n) % n;
    tryFile (list[(size_t) index].file);
}

void LibraryController::tryFile (const juce::File& file)
{
    juce::String error;
    const auto type = presetTypeOf (file);
    if (! proc.loadPresetFile (file, voice(), pattern(), BatidaProcessor::LoadMode::Browse, &error))
    {
        showMessage ("Couldn't load", error);
        return;
    }
    if (type == PresetType::Sound)
        auditionOnce (voice());
    if (onChanged)
        onChanged();
}

void LibraryController::reload (PresetType type)
{
    juce::String error;
    if (! proc.reloadOrigin (type, voice(), pattern(), &error))
    {
        if (error.isNotEmpty())
            showMessage ("Couldn't load", error);
        return;
    }
    if (type == PresetType::Sound)
        auditionOnce (voice());
    if (onChanged)
        onChanged();
}

void LibraryController::trySample (const juce::File& file)
{
    if (! proc.browseSample (voice(), file))
    {
        showMessage ("Couldn't load", proc.sampleSlot (voice()).getError());
        return;
    }
    auditionOnce (voice());
    if (onChanged)
        onChanged();
}

void LibraryController::auditionOnce (int v)
{
    // With a pattern playing you hear it in the beat; otherwise, one hit.
    if (proc.sequencer().isRunning())
        return;
    proc.audition (v, true);
    auto* p = &proc;
    juce::WeakReference<LibraryController> safe (this);
    juce::Timer::callAfterDelay (250, [safe, p, v] { if (safe != nullptr) p->audition (v, false); });
}

void LibraryController::init (PresetType type)
{
    switch (type)
    {
        case PresetType::Sound:   proc.initSound (voice()); break;
        case PresetType::Kit:     proc.initKit(); break;
        case PresetType::Pattern: proc.initPattern (pattern()); break;
        case PresetType::Set:     proc.initAll(); break;
    }
    if (onChanged)
        onChanged();
}

void LibraryController::loadFromDisk (PresetType type)
{
    const auto ext = juce::String (extensionFor (type));
    chooser = std::make_unique<juce::FileChooser> ("Load a " + juce::String (typeName (type)),
                                                   proc.library().folderFor (type, false), "*" + ext);
    juce::WeakReference<LibraryController> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || ! fc.getResult().existsAsFile())
                                  return;
                              juce::String error;
                              if (! safe->proc.loadPresetFile (fc.getResult(), safe->voice(), safe->pattern(),
                                                               BatidaProcessor::LoadMode::Step, &error))
                                  showMessage ("Couldn't load", error);
                              else if (safe->onChanged)
                                  safe->onChanged();
                          });
}

void LibraryController::save (PresetType type)
{
    const auto file = currentFile (type);
    if (! file.existsAsFile() || proc.library().isFactory (file) || presetTypeOf (file) != type)
    {
        saveAs (type);
        return;
    }
    auto info = readInfo (file).value_or (PresetInfo {});
    info.name = currentName (type);
    if (info.author.isEmpty())
        info.author = proc.library().getAuthor();
    juce::String error;
    bool ok = false;
    switch (type)
    {
        case PresetType::Sound:   ok = proc.saveSound (voice(), file, info, false, &error); break;
        case PresetType::Kit:     ok = proc.saveKit (file, info, false, &error); break;
        case PresetType::Pattern: ok = proc.savePattern (pattern(), file, info, &error); break;
        case PresetType::Set:     ok = proc.saveSet (file, info, false, &error); break;
    }
    if (! ok)
        showMessage ("Couldn't save", error);
}

void LibraryController::saveAs (PresetType type)
{
    closeOverlays();
    savePanel = std::make_unique<SavePanel> (proc, type, voice(), pattern());
    savePanel->setComponentID ("savePanel");
    juce::WeakReference<LibraryController> safe (this);
    savePanel->onClose = [safe]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->savePanel = nullptr; });
    };
    parent.addAndMakeVisible (*savePanel);
    layoutOverlays();
    if (auto* nameField = savePanel->findChildWithID ("saveName"))
        if (nameField->isShowing())
            nameField->grabKeyboardFocus();
}

void LibraryController::showRelink()
{
    closeOverlays();
    relinkPanel = std::make_unique<RelinkPanel> (proc);
    relinkPanel->setComponentID ("relinkPanel");
    juce::WeakReference<LibraryController> safe (this);
    relinkPanel->onClose = [safe]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->relinkPanel = nullptr; });
    };
    parent.addAndMakeVisible (*relinkPanel);
    layoutOverlays();
}

void LibraryController::collectSamples()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder to copy this project's samples into",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    juce::WeakReference<LibraryController> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories
                              | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
                          {
                              auto folder = fc.getResult();
                              if (safe == nullptr || folder == juce::File())
                                  return;
                              if (! folder.isDirectory())
                                  folder = folder.getParentDirectory();
                              juce::String error;
                              if (! safe->proc.collectSamples (folder, &error))
                                  showMessage ("Couldn't collect the samples", error);
                          });
}

void LibraryController::closeOverlays()
{
    savePanel = nullptr;
    relinkPanel = nullptr;
}

void LibraryController::layoutOverlays()
{
    for (juce::Component* c : { (juce::Component*) savePanel.get(), (juce::Component*) relinkPanel.get() })
        if (c != nullptr)
            c->setBounds (parent.getLocalBounds());
}

void LibraryController::showMenu (PresetType type, juce::Component& target)
{
    const auto noun = juce::String (typeName (type));

    auto addLevel = [&] (juce::PopupMenu& m, PresetType t, int base, const juce::String& n)
    {
        m.addItem (base + 1, "Init " + n);
        m.addItem (base + 2, "Load " + n + "...");
        const auto f = currentFile (t);
        const auto own = f.existsAsFile() && ! proc.library().isFactory (f);
        m.addItem (base + 3, own ? "Save " + n + " (" + f.getFileName() + ")" : "Save " + n + "...");
        m.addItem (base + 4, "Save " + n + " as...");
    };

    juce::PopupMenu m;
    m.addSectionHeader (capitalised (noun) + ": " + currentName (type));
    addLevel (m, type, 0, noun);
    if (type == PresetType::Kit)
    {
        juce::PopupMenu set;
        addLevel (set, PresetType::Set, 10, "everything");
        m.addSeparator();
        m.addSubMenu ("Everything (kit, patterns and settings)", set);
        m.addSeparator();
        const auto missing = proc.numMissingSamples();
        m.addItem (21, missing > 0 ? "Relink " + juce::String (missing) + " missing sample" + (missing == 1 ? "" : "s") + "..."
                                   : juce::String ("Relink samples..."),
                   missing > 0);
        m.addItem (22, "Collect this project's samples into a folder...");
        m.addItem (23, "Show library in Finder");
    }

    juce::WeakReference<LibraryController> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [safe, type] (int choice)
    {
        if (safe == nullptr || choice == 0)
            return;
        const auto t = choice > 10 && choice < 20 ? PresetType::Set : type;
        switch (choice % 10)
        {
            case 1: if (choice < 20) safe->init (t); break;
            case 2: if (choice < 20) safe->loadFromDisk (t); break;
            case 3: if (choice < 20) safe->save (t); break;
            case 4: if (choice < 20) safe->saveAs (t); break;
            default: break;
        }
        if (choice == 21) safe->showRelink();
        if (choice == 22) safe->collectSamples();
        if (choice == 23) safe->proc.library().getRoot().revealToUser();
    });
}
