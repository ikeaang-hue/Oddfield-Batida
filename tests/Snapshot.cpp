// Developer tool: renders the plugin editor offscreen and saves one PNG per
// tab, so the layout can be reviewed without a host.
//
//   BatidaSnapshot <out-dir> [sample.wav]

#include "Plugin/PluginProcessor.h"

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto outDir = cwd.getChildFile (argc > 1 ? argv[1] : "snapshots");
    outDir.createDirectory();

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
    tabs->setVisible (true);
    for (int i = 0; i < tabs->getNumTabs(); ++i)
    {
        tabs->setCurrentTabIndex (i);
        save (juce::String (i + 1) + "-" + tabs->getTabNames()[i].replace (" ", "").replace ("&", ""));
    }

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

        // State keeps names.
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
