// Developer tool: renders the plugin editor offscreen and saves one PNG per
// tab, so the layout can be reviewed without a host.
//
//   BatidaSnapshot <out-dir> [sample.wav]

#include "Plugin/PluginProcessor.h"
#include "UI/LibraryPage.h"

namespace
{
juce::File writeTone (const juce::File& file, float hz)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::AudioBuffer<float> b (1, 4800);
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample (0, i, 0.5f * std::sin (6.2831853f * hz * (float) i / 48000.0f));
    std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
    juce::WavAudioFormat wav;
    if (auto w = wav.createWriterFor (out, juce::AudioFormatWriterOptions {}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (16)))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return file;
}

juce::Button* findButton (juce::Component& parent, const juce::String& text)
{
    for (auto* c : parent.getChildren())
    {
        if (auto* b = dynamic_cast<juce::Button*> (c); b != nullptr && b->getButtonText() == text)
            return b;
        if (auto* b = findButton (*c, text))
            return b;
    }
    return nullptr;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto outDir = cwd.getChildFile (argc > 1 ? argv[1] : "snapshots");
    outDir.createDirectory();

    // A library of its own, so the checks never touch the real one.
    const auto libDir = outDir.getParentDirectory().getChildFile ("snapshot-library");
    libDir.deleteRecursively();
    setenv ("BATIDA_LIBRARY", libDir.getFullPathName().toRawUTF8(), 1);
    const auto sampleDir = outDir.getParentDirectory().getChildFile ("snapshot-samples");
    sampleDir.deleteRecursively();

    BatidaProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    if (argc > 2)
        proc.loadSample (0, cwd.getChildFile (argv[2]));

    // Turn two macros and move the pad, so the effective-value markers show.
    auto& state = proc.getState();
    state.getParameter ("v1_fm_bright")->setValueNotifyingHost (0.75f);
    state.getParameter ("v1_fm_harm")->setValueNotifyingHost (0.7f);
    state.getParameter ("xy_x")->setValueNotifyingHost (0.7f);
    state.getParameter ("xy_y")->setValueNotifyingHost (0.55f);
    state.getParameter ("dist_drive")->setValueNotifyingHost (0.1f);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    auto* tabs = dynamic_cast<juce::TabbedComponent*> (editor->findChildWithID ("tabs"));
    auto* kit = editor->findChildWithID ("kitPage");
    if (tabs == nullptr || kit == nullptr)
        return 1;

    auto save = [&] (const juce::String& name)
    {
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = outDir.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::printf ("%s\n", file.getFullPathName().toRawUTF8());
    };

    save ("0-Kit"); // the opening page

    auto* seq = editor->findChildWithID ("seqPage");
    kit->setVisible (false);
    if (seq != nullptr)
    {
        seq->setVisible (true);
        save ("0-Seq");
        seq->setVisible (false);
    }
    // Put a voice target on Mod 2 so the MOD page shows a list, and the knob shows the arc.
    proc.toggleModTarget (1, "v1_flt_cutoff");
    if (auto* mod = editor->findChildWithID ("modPage"))
    {
        mod->setVisible (true);
        save ("0-Mod");
        mod->setVisible (false);
    }
    tabs->setVisible (true);
    if (auto* strip = editor->findChildWithID ("soundStrip"))
        strip->setVisible (true);
    proc.toggleModTarget (1, "v1_flt_cutoff"); // shows the modulation arc on the Voice FX cutoff knob
    for (int i = 0; i < tabs->getNumTabs(); ++i)
    {
        tabs->setCurrentTabIndex (i);
        save (juce::String (i + 1) + "-" + tabs->getTabNames()[i].replace (" ", "").replace ("&", ""));
    }

    proc.toggleModTarget (1, "v1_flt_cutoff");
    if (auto* strip = editor->findChildWithID ("soundStrip"))
        strip->setVisible (false);

    // Gesture checks on the real editor: synthetic mouse events into the grid
    // and the tempo control.
    auto* grid = editor->findChildWithID ("seqPage") != nullptr ? editor->findChildWithID ("seqPage")->findChildWithID ("grid") : nullptr;
    auto* tempo = editor->findChildWithID ("seqPage") != nullptr ? editor->findChildWithID ("seqPage")->findChildWithID ("tempo") : nullptr;
    if (grid != nullptr && tempo != nullptr)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        auto event = [&] (juce::Component* c, juce::Point<float> down, juce::Point<float> at, bool dragged,
                          juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, c, now, down, now, 1, dragged);
        };
        auto gesture = [&] (juce::Component* c, juce::Point<float> from, std::initializer_list<juce::Point<float>> path)
        {
            c->mouseDown (event (c, from, from, false));
            auto last = from;
            for (auto p : path)
            {
                c->mouseDrag (event (c, from, p, true));
                last = p;
            }
            c->mouseUp (event (c, from, last, ! empty (path), juce::ModifierKeys()));
        };

        const auto& bank = proc.patterns().get();
        auto stepOf = [&] (int track, int step) { return bank.patterns[0].tracks[(size_t) track].steps[(size_t) step]; };

        // Cell centre of (track, step) on page 1.
        const auto rh = (float) grid->getHeight() / (batida::kNumTracks + 1);
        const auto cw = (float) (grid->getWidth() - 130) / 16.0f;
        auto cell = [&] (int track, int step) { return juce::Point<float> (130.0f + (step + 0.5f) * cw, (track + 0.5f) * rh); };

        int failures = 0;
        auto check = [&] (bool ok, const char* what) { std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what); failures += ok ? 0 : 1; };

        const auto wasOn = stepOf (3, 1).gate;
        gesture (grid, cell (3, 1), {});
        check (stepOf (3, 1).gate != wasOn, "tap toggles a step");
        gesture (grid, cell (3, 1), {});
        check (stepOf (3, 1).gate == wasOn, "tap again toggles it back");

        const auto v0 = stepOf (0, 0).velocity; // kick, step 1 (on, 118)
        gesture (grid, cell (0, 0), { cell (0, 0).translated (0, -4), cell (0, 0).translated (0, -10) });
        check (stepOf (0, 0).gate && stepOf (0, 0).velocity == juce::jmin (127, v0 + 8), "drag up raises velocity from where it was");
        gesture (grid, cell (0, 0), { cell (0, 0).translated (0, 40) });
        check (stepOf (0, 0).gate && stepOf (0, 0).velocity < v0, "drag down lowers it");
        gesture (grid, cell (0, 0), { cell (0, 0).translated (0, 300) });
        check (! stepOf (0, 0).gate, "drag to the bottom turns the step off");
        gesture (grid, cell (4, 5), { cell (4, 5).translated (0, -80) });
        check (stepOf (4, 5).gate && stepOf (4, 5).velocity == 64, "drag up on an empty step turns it on at that velocity");

        auto& tempoParam = *proc.getState().getParameter ("seq_tempo");
        auto bpm = [&] { return tempoParam.convertFrom0to1 (tempoParam.getValue()); };
        const auto t0 = bpm();
        const juce::Point<float> intPart (12.0f, tempo->getHeight() / 2.0f);
        gesture (tempo, intPart, { intPart.translated (0, -10) });
        check (std::abs (bpm() - (t0 + 2.0f)) < 1.0e-3f, "tempo: dragging the whole number moves by 1 per step, from the set value");
        const juce::Point<float> fracPart (46.0f, tempo->getHeight() / 2.0f);
        gesture (tempo, fracPart, { fracPart.translated (0, -15) });
        check (std::abs (bpm() - (t0 + 2.03f)) < 1.0e-3f, "tempo: dragging the decimals moves by 0.01 per step");
        std::printf ("  tempo now %.2f (was %.2f)\n", bpm(), t0);

        // Modulation: assign, display, remove.
        check (proc.isModTarget (1, "v1_flt_cutoff"), "right-click assign: Mod 2 now drives V1 Cutoff");
        check (proc.modDisplayFor ("v1_flt_cutoff").has_value(), "the knob gets a live modulation display");
        proc.toggleModTarget (1, "v1_flt_cutoff");
        check (! proc.isModTarget (1, "v1_flt_cutoff") && ! proc.modDisplayFor ("v1_flt_cutoff").has_value(), "assigning again removes it");

        // Shape editor: click adds a point, double-click deletes it.
        std::function<juce::Component* (juce::Component*, const juce::String&)> findDeep = [&] (juce::Component* c, const juce::String& id) -> juce::Component*
        {
            for (auto* child : c->getChildren())
            {
                if (child->getComponentID() == id)
                    return child;
                if (auto* found = findDeep (child, id))
                    return found;
            }
            return nullptr;
        };
        bool shapeChecked = false;
        if (auto* modPage = editor->findChildWithID ("modPage"))
            if (auto* shape = findDeep (modPage, "shape1"))
            {
                shapeChecked = true;
                modPage->setVisible (true);
                const auto before = proc.movement().get().mods[0].numPoints;
                const juce::Point<float> at (shape->getWidth() * 0.8f, shape->getHeight() * 0.3f);
                gesture (shape, at, {});
                check (proc.movement().get().mods[0].numPoints == before + 1, "shape: click adds a point");
                shape->mouseDoubleClick (event (shape, at, at, false));
                check (proc.movement().get().mods[0].numPoints == before, "shape: double-click deletes it");
                modPage->setVisible (false);
            }
        check (shapeChecked, "found the shape editor");

        // Undo a step edit.
        {
            const auto was = stepOf (4, 7).gate;
            gesture (grid, cell (4, 7), {});
            check (stepOf (4, 7).gate != was, "tap changes a step");
            proc.undo();
            check (stepOf (4, 7).gate == was, "Undo restores it");
            proc.redo();
            check (stepOf (4, 7).gate != was, "Redo re-applies it");
            proc.undo();
        }

        // Scenes: store, change, recall, undo the recall.
        {
            auto* drive = proc.getState().getParameter ("dist_drive");
            auto value = [&] { return drive->convertFrom0to1 (drive->getValue()); };
            drive->setValueNotifyingHost (drive->convertTo0to1 (0.3f));
            proc.storeScene (0);
            drive->setValueNotifyingHost (drive->convertTo0to1 (0.7f));
            proc.recallScene (0);
            check (std::abs (value() - 0.3f) < 1.0e-3f, "recall brings the scene back");
            proc.undo();
            check (std::abs (value() - 0.7f) < 1.0e-3f, "Undo reverts the recall");
        }

        // Vary: suggest, preview, keep, undo.
        {
            auto snare = [&] { return proc.readVoiceParams (2); };
            const auto before = snare();
            proc.startVary (2, 0.4f, batida::VaryDirection::None, false, false, false);
            for (int i = 0; i < 200 && proc.isVarying(); ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            check (proc.varyCandidates().size() >= 3, "Vary finds at least 3 suggestions");
            std::printf ("  %d suggestions\n", (int) proc.varyCandidates().size());
            proc.previewCandidate (0);
            check (snare()[batida::vp::FmBright] == before[batida::vp::FmBright]
                       && snare()[batida::vp::AmpD] == before[batida::vp::AmpD], "preview doesn't touch the project");
            const auto expectedLevel = BatidaProcessor::withLiveMix (proc.varyCandidates()[0], before)[batida::vp::Level];
            proc.keepCandidate();
            bool changed = false;
            const auto after = snare();
            check (std::abs (after[batida::vp::Level] - expectedLevel) < 0.05f, "Keep sets the level-matched Level");
            std::printf ("  Level %.2f -> %.2f dB\n", before[batida::vp::Level], after[batida::vp::Level]);
            for (int k = 0; k < batida::kNumVoiceParams; ++k)
                changed = changed || std::abs (after[k] - before[k]) > 1.0e-5f;
            check (changed, "Keep writes the suggestion into the voice");
            proc.undo();
            bool restored = true;
            const auto undone = snare();
            for (int k = 0; k < batida::kNumVoiceParams; ++k)
                restored = restored && std::abs (undone[k] - before[k]) < 1.0e-3f * std::max (1.0f, std::abs (before[k]));
            check (restored, "Undo brings the original back");
        }

        // Rename and swap.
        proc.setVoiceName (2, "Crack");
        check (proc.getVoiceName (2) == "Crack", "rename a voice");
        auto pitchOf = [&] (int v) { auto* q = proc.getState().getParameter (batida::voiceParamID (v, batida::vp::FmPitch)); return q->convertFrom0to1 (q->getValue()); };
        const auto p0 = pitchOf (0), p2 = pitchOf (2);
        const auto kickTrack = proc.patterns().get().patterns[0].tracks[0].steps;
        const auto snareTrack = proc.patterns().get().patterns[0].tracks[2].steps;
        auto sameSteps = [] (const auto& a, const auto& b)
        {
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i].gate != b[i].gate || a[i].velocity != b[i].velocity) return false;
            return true;
        };
        proc.swapVoices (0, 2);
        check (std::abs (pitchOf (0) - p2) < 1.0e-3f && std::abs (pitchOf (2) - p0) < 1.0e-3f, "swap moves the sounds' settings");
        check (proc.getVoiceName (0) == "Crack" && proc.getVoiceName (2) == "Kick", "swap moves the names");
        check (sameSteps (proc.patterns().get().patterns[0].tracks[0].steps, snareTrack)
                   && sameSteps (proc.patterns().get().patterns[0].tracks[2].steps, kickTrack),
               "swap moves the pattern tracks");
        proc.swapVoices (0, 2);
        check (std::abs (pitchOf (0) - p0) < 1.0e-3f && proc.getVoiceName (2) == "Crack", "swapping back restores them");

        // Library: browse, merged undo, favourites, strips, save, init, sets, relink.
        {
            using namespace batida;
            auto& lib = proc.library();
            lib.scanNow();
            auto* libPage = dynamic_cast<LibraryPage*> (editor->findChildWithID ("libraryPage"));
            check (libPage != nullptr && lib.getRoot().getChildFile ("Factory/Kits/Neutral.batida-kit").existsAsFile(),
                   "the factory library is installed");
            tabs->setVisible (false);
            libPage->setVisible (true);
            libPage->setMode (LibraryPage::Mode::Sounds);
            check (libPage->getNumShown() == 8, "the 8 factory sounds are listed");
            auto rowOf = [&] (PresetType type, const juce::String& n)
            {
                const auto list = lib.shown (type);
                for (size_t i = 0; i < list.size(); ++i)
                    if (list[i].info.name == n)
                        return (int) i;
                return -1;
            };
            proc.selectedVoice = 0;
            const auto before = proc.readVoiceParams (0);
            const auto nameBefore = proc.getVoiceName (0);
            libPage->clickRow (rowOf (PresetType::Sound, "Snare"));
            check (proc.getVoiceName (0) == "Snare"
                       && std::abs (proc.readVoiceParams (0)[vp::FmPitch] - defaultKitParams().voices[2][vp::FmPitch]) < 1.0e-3f,
                   "clicking a sound loads it into the selected voice");
            libPage->clickRow (rowOf (PresetType::Sound, "Open Hat"));
            check (proc.getVoiceName (0) == "Open Hat", "clicking another tries that one");
            save ("6-Lib");
            proc.undo();
            check (proc.getVoiceName (0) == nameBefore && std::abs (proc.readVoiceParams (0)[vp::AmpD] - before[vp::AmpD]) < 1.0e-3f
                       && std::abs (proc.readVoiceParams (0)[vp::FmBright] - before[vp::FmBright]) < 1.0e-3f,
                   "one Undo goes back to before browsing");

            const auto kickFile = lib.getRoot().getChildFile ("Factory/Sounds/Kick/Kick.batida-sound");
            libPage->clickRow (rowOf (PresetType::Sound, "Kick"), 5);
            check (lib.isFavourite (kickFile) && proc.getVoiceName (0) == nameBefore, "the heart marks a favourite without loading");

            libPage->setMode (LibraryPage::Mode::Kits);
            save ("6-LibKits");
            if (auto* strip = editor->findChildWithID ("kitStrip"))
                if (auto* nextButton = dynamic_cast<juce::Button*> (strip->getChildComponent (2)))
                    nextButton->onClick();
            check (proc.getOrigins().kitName == "Neutral" && proc.getOrigins().kitFile.existsAsFile(), "the kit strip steps to a library kit");

            // Save a sound through the panel; the same name asks before replacing.
            libPage->setMode (LibraryPage::Mode::Sounds);
            auto saveThroughPanel = [&] (const juce::String& name, int clicks)
            {
                if (auto* b = findButton (*libPage, "Save..."))
                    b->onClick();
                auto* panel = editor->findChildWithID ("savePanel");
                if (panel == nullptr)
                    return juce::String ("no panel");
                if (auto* field = dynamic_cast<juce::TextEditor*> (panel->findChildWithID ("saveName")))
                    field->setText (name, true);
                auto* button = dynamic_cast<juce::Button*> (panel->findChildWithID ("saveButton"));
                for (int i = 0; i < clicks && button != nullptr; ++i)
                    button->onClick();
                const auto text = button != nullptr ? button->getButtonText() : juce::String();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                return text;
            };
            if (auto* b = findButton (*libPage, "Save..."))
                b->onClick();
            if (auto* panel = editor->findChildWithID ("savePanel"))
            {
                if (auto* field = dynamic_cast<juce::TextEditor*> (panel->findChildWithID ("saveName")))
                    field->setText ("Test Kick", true);
                save ("7-Save");
                if (auto* cancel = findButton (*panel, "Cancel"))
                    cancel->onClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            }
            saveThroughPanel ("Test Kick", 1);
            const auto mine = lib.getRoot().getChildFile ("User/Sounds/Kick/Test Kick.batida-sound");
            check (mine.existsAsFile() && proc.getVoiceName (0) == "Test Kick", "Save puts it in the library and names the voice");
            const auto firstSave = mine.getLastModificationTime();
            const auto armed = saveThroughPanel ("Test Kick", 1);
            check (armed == "Replace", "saving the same name asks first (Replace)");
            if (auto* panel = editor->findChildWithID ("savePanel"))
                if (auto* cancel = findButton (*panel, "Cancel"))
                    cancel->onClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            check (mine.getLastModificationTime() == firstSave, "and doesn't replace it until asked");
            lib.scanNow();
            check (rowOf (PresetType::Sound, "Test Kick") >= 0, "the saved sound is in the list");

            // Init and undo.
            proc.initSound (3);
            check (proc.getVoiceName (3) == "Init", "Init sound gives a plain sound");
            proc.undo();
            check (proc.getVoiceName (3) == "Clap", "and Undo brings the sound back");

            // A set holds everything: save, init everything, load it back.
            auto* tempoParam = proc.getState().getParameter (globalParamID (gp::SeqTempo));
            tempoParam->setValueNotifyingHost (tempoParam->convertTo0to1 (97.0f));
            proc.setVoiceName (5, "Sub");
            const auto setFile = lib.userFileFor (PresetType::Set, { "My Set", "", "", {}, {} });
            check (proc.saveSet (setFile, { "My Set", "", "", {}, {} }, false), "save a set");
            proc.initAll();
            check (std::abs (tempoParam->convertFrom0to1 (tempoParam->getValue()) - 120.0f) < 0.01f && proc.getVoiceName (5) == "Bass",
                   "Init everything resets the project");
            proc.loadPresetFile (setFile, 0, 0, BatidaProcessor::LoadMode::Step);
            check (std::abs (tempoParam->convertFrom0to1 (tempoParam->getValue()) - 97.0f) < 0.01f && proc.getVoiceName (5) == "Sub"
                       && ! proc.patterns().get().patterns[0].isEmpty(),
                   "loading the set brings it all back");

            // A pattern file into slot 2.
            proc.loadPresetFile (lib.getRoot().getChildFile ("Factory/Patterns/Breakbeat.batida-pattern"), 0, 1, BatidaProcessor::LoadMode::Step);
            check (! proc.patterns().get().patterns[1].isEmpty() && proc.getPatternName (1) == "Breakbeat", "a pattern loads into its slot");

            // Missing samples: move a sample away, reopen, relink.
            const auto tone = writeTone (sampleDir.getChildFile ("a/relink-me.wav"), 330.0f);
            proc.loadSample (4, tone);
            juce::MemoryBlock withSample;
            proc.getStateInformation (withSample);
            const auto moved = sampleDir.getChildFile ("b/relink-me.wav");
            moved.getParentDirectory().createDirectory();
            tone.moveFileTo (moved);
            proc.clearSample (4); // as if the project were opened fresh
            proc.setStateInformation (withSample.getData(), (int) withSample.getSize());
            check (proc.numMissingSamples() == 1, "a moved sample shows as missing");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300); // the editor's timer updates the header
            if (auto* b = findButton (*editor, "1 sample missing: Relink..."))
            {
                b->onClick();
                save ("7-Relink");
                if (auto* panel = editor->findChildWithID ("relinkPanel"))
                    if (auto* close = findButton (*panel, "Close"))
                        close->onClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            }
            else
                check (false, "the header shows the missing-sample notice");
            proc.relinkSample (4, moved);
            check (proc.numMissingSamples() == 0 && proc.sampleSlot (4).getPath() == moved.getFullPathName(), "Locate relinks it");
            // Next time it's found on its own: Batida remembers where samples turned up.
            BatidaProcessor other;
            proc.getStateInformation (withSample);
            other.setStateInformation (withSample.getData(), (int) withSample.getSize());
            check (other.numMissingSamples() == 0, "a relinked project opens with its sample");

            libPage->setMode (LibraryPage::Mode::Samples);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            save ("6-LibSamples");
            libPage->setVisible (false);
        }

        // State keeps names.
        proc.setVoiceName (2, "Crack");
        juce::MemoryBlock saved;
        proc.getStateInformation (saved);
        BatidaProcessor other;
        other.setStateInformation (saved.getData(), (int) saved.getSize());
        check (other.getVoiceName (2) == "Crack" && other.getVoiceName (1) == "Rim", "names are saved with the project");
        std::printf ("%s\n", failures == 0 ? "GESTURES PASS" : "GESTURES FAIL");
    }

    editor.reset();
    return 0;
}
