#include "Tools.h"

#include <juce_gui_basics/juce_gui_basics.h>

// BatidaTests                 run the unit tests
// BatidaTests --bench         measure CPU for the worst-case voice load
// BatidaTests --render <dir>  render the default kit to WAV files
// BatidaTests --pad <dir>     loudness of a beat across the XY pad, plus WAVs
// BatidaTests --beat <dir>    the breakbeat (pattern 1) at three pad positions
// BatidaTests --vary-report   how often Vary finds nothing, over real sounds
int main (int argc, char* argv[])
{
    const juce::StringArray args (argv + 1, argc - 1);

    if (args.contains ("--bench"))
        return batida::tools::runBenchmark();

    if (const auto i = args.indexOf ("--beat"); i >= 0)
        return batida::tools::runBeat (juce::File::getCurrentWorkingDirectory().getChildFile (i + 1 < args.size() ? args[i + 1] : "renders"));

    if (const auto i = args.indexOf ("--pad"); i >= 0)
        return batida::tools::runPad (juce::File::getCurrentWorkingDirectory().getChildFile (i + 1 < args.size() ? args[i + 1] : "renders"));

    if (const auto i = args.indexOf ("--render"); i >= 0)
    {
        const auto dir = i + 1 < args.size() ? juce::File::getCurrentWorkingDirectory().getChildFile (args[i + 1])
                                             : juce::File::getCurrentWorkingDirectory().getChildFile ("renders");
        return batida::tools::runRender (dir);
    }

    // The processor tests need a message thread (timers, async callbacks).
    juce::ScopedJuceInitialiser_GUI gui;
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory (args.contains ("--vary-report") ? "Report" : "Batida");

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
