#include "Engine/Movement/Vary.h"
#include "Library/PresetFiles.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

// How often Vary comes back empty ("no usable variations"): the default kit's
// eight FM sounds and 16 factory sounds (mostly rendered samples), in every
// direction, at Subtle and Medium. A report, not a pass/fail test (it takes a
// few minutes): BatidaTests --vary-report

namespace batida
{

class VaryReportTests final : public juce::UnitTest
{
public:
    VaryReportTests() : juce::UnitTest ("Vary report", "Report") {}

    void runTest() override
    {
        beginTest ("How often Vary finds nothing");

        const char* names[] = { "any", "brighter", "darker", "shorter", "longer", "tonal", "noisy", "cleaner", "dirtier" };
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        struct Case { juce::String name; VoiceParams params; std::shared_ptr<SampleData> sample; };
        std::vector<Case> cases;
        const auto kit = defaultKitParams();
        for (int v = 0; v < kNumVoices; ++v)
            cases.push_back ({ juce::String ("default ") + defaultVoiceName (v), kit.voices[(size_t) v], nullptr });

        auto files = juce::File (BATIDA_SOURCE_DIR).getChildFile ("resources/factory/Sounds")
                         .findChildFiles (juce::File::findFiles, true, "*.batida-sound");
        files.sort();
        const auto step = std::max (1, files.size() / 16);
        for (int i = 0; i < files.size() && cases.size() < 24; i += step)
            if (const auto s = readSound (files[i]))
            {
                std::shared_ptr<SampleData> data;
                if (! s->sample.isEmpty())
                {
                    juce::String note;
                    data = SampleSlot::decode (juce::File (s->sample.path), formats, note);
                    if (data == nullptr)
                        continue;
                }
                cases.push_back ({ files[i].getFileNameWithoutExtension(), s->params, data });
            }

        int empty = 0, total = 0, suggestions = 0;
        juce::StringArray missing;
        for (const auto amount : { 0.18f, 0.4f })
            for (const auto& c : cases)
                for (int d = 0; d < 9; ++d)
                {
                    VaryRequest req;
                    req.base = c.params;
                    req.sample = c.sample.get();
                    req.amount = amount;
                    req.direction = (VaryDirection) d;
                    req.seed = (uint32_t) (1000 + total);
                    const auto n = (int) vary (req).size();
                    suggestions += n;
                    ++total;
                    if (n == 0)
                    {
                        ++empty;
                        missing.add (juce::String (amount < 0.3f ? "subtle " : "medium ") + c.name + " / " + names[d]);
                    }
                }
        logMessage ("  " + juce::String ((int) cases.size()) + " sounds; empty: " + juce::String (empty) + " of " + juce::String (total)
                    + "; suggestions per try: " + juce::String ((double) suggestions / total, 2));
        for (const auto& m : missing)
            logMessage ("    nothing for " + m);
    }
};

static VaryReportTests varyReportTests;

} // namespace batida
