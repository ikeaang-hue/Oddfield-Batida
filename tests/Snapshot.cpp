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

    kit->setVisible (false);
    tabs->setVisible (true);
    for (int i = 0; i < tabs->getNumTabs(); ++i)
    {
        tabs->setCurrentTabIndex (i);
        save (juce::String (i + 1) + "-" + tabs->getTabNames()[i].replace (" ", "").replace ("&", ""));
    }

    editor.reset();
    return 0;
}
