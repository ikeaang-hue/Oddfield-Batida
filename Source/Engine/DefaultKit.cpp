#include "Parameters.h"

#include <cassert>
#include <utility>

// Starting sounds for the 8 slots. Each slot is general-purpose; these are only
// defaults so the kit makes sound before any editing or sample loading.
// FM pitch is in semitones from C3 (261.6 Hz). Choice values are indices:
// fm_algo 0-7 = algorithms 1-8, play_mode 1 = Gate, drive_type 0-3 =
// Soft/Hard/Fold/Crush, flt_type 0-2 = LP/HP/BP.

namespace batida
{

namespace
{
using Overrides = std::vector<std::pair<const char*, float>>;

struct DefaultVoice
{
    const char* name;
    Overrides values;
};

const std::array<DefaultVoice, kNumVoices>& defaultVoices()
{
    static const std::array<DefaultVoice, kNumVoices> voices { {
        { "Kick",
          { { "fm_pitch", -29.4f }, { "fm_algo", 3 },
            { "op2_level", 0.3f }, { "op2_decay", 35.0f }, { "op2_sustain", 0.0f }, { "op2_release", 35.0f },
            { "pitch_amt", 28.0f }, { "pitch_decay", 150.0f },
            { "amp_decay", 500.0f }, { "amp_release", 50.0f },
            { "punch", 0.3f }, { "drive", 0.2f } } },

        { "Rim",
          { { "fm_pitch", 23.0f }, { "fm_algo", 3 },
            { "op1_level", 0.8f },
            { "op2_ratio", 2.41f }, { "op2_level", 0.45f }, { "op2_decay", 40.0f }, { "op2_sustain", 0.0f }, { "op2_release", 20.0f },
            { "op3_ratio", 1.53f }, { "op3_level", 0.5f }, { "op3_decay", 50.0f }, { "op3_sustain", 0.0f }, { "op3_release", 20.0f },
            { "op4_ratio", 4.1f }, { "op4_level", 0.3f }, { "op4_decay", 30.0f }, { "op4_sustain", 0.0f }, { "op4_release", 20.0f },
            { "pitch_amt", 4.0f }, { "pitch_decay", 20.0f },
            { "amp_decay", 70.0f }, { "amp_release", 30.0f },
            { "flt_type", 1 }, { "flt_cutoff", 350.0f }, { "flt_res", 0.25f },
            { "drive_type", 1 }, { "drive", 0.15f }, { "level", -4.0f } } },

        { "Snare",
          { { "fm_pitch", -6.5f }, { "fm_algo", 5 }, { "fm_feedback", 0.95f },
            { "op1_level", 0.7f }, { "op1_decay", 180.0f }, { "op1_sustain", 0.0f }, { "op1_release", 60.0f },
            { "op2_ratio", 1.62f }, { "op2_level", 0.35f }, { "op2_decay", 120.0f }, { "op2_sustain", 0.0f }, { "op2_release", 60.0f },
            { "op3_ratio", 3.1f }, { "op3_level", 0.6f }, { "op3_decay", 260.0f }, { "op3_sustain", 0.0f }, { "op3_release", 80.0f },
            { "op4_ratio", 5.3f }, { "op4_level", 0.9f }, { "op4_decay", 300.0f }, { "op4_sustain", 0.0f }, { "op4_release", 80.0f },
            { "pitch_amt", 9.0f }, { "pitch_decay", 60.0f },
            { "amp_decay", 300.0f }, { "amp_release", 60.0f },
            { "flt_cutoff", 16000.0f },
            { "punch", 0.45f }, { "drive", 0.3f }, { "level", -3.0f } } },

        { "Tom",
          { { "fm_pitch", -15.0f }, { "fm_algo", 3 },
            { "op2_ratio", 1.5f }, { "op2_level", 0.25f }, { "op2_decay", 80.0f }, { "op2_sustain", 0.0f }, { "op2_release", 40.0f },
            { "pitch_amt", 10.0f }, { "pitch_decay", 200.0f },
            { "amp_decay", 600.0f }, { "amp_release", 60.0f },
            { "punch", 0.2f }, { "drive", 0.15f }, { "level", -2.0f } } },

        { "Zap",
          { { "fm_pitch", 0.0f }, { "fm_algo", 0 }, { "fm_feedback", 0.3f },
            { "op1_level", 0.9f },
            { "op2_ratio", 2.0f }, { "op2_level", 0.5f }, { "op2_decay", 150.0f }, { "op2_sustain", 0.0f }, { "op2_release", 60.0f },
            { "op3_ratio", 3.5f }, { "op3_level", 0.4f }, { "op3_decay", 100.0f }, { "op3_sustain", 0.0f }, { "op3_release", 50.0f },
            { "op4_ratio", 1.0f }, { "op4_level", 0.3f }, { "op4_decay", 80.0f }, { "op4_sustain", 0.0f }, { "op4_release", 40.0f },
            { "pitch_amt", 40.0f }, { "pitch_decay", 250.0f },
            { "amp_decay", 300.0f },
            { "drive_type", 2 }, { "drive", 0.35f },
            { "flt_cutoff", 9000.0f }, { "flt_res", 0.3f }, { "level", -2.0f } } },

        { "Bass",
          { { "play_mode", 1 }, { "glide", 60.0f },
            { "fm_pitch", -24.0f }, { "fm_algo", 3 },
            { "op1_decay", 600.0f }, { "op1_release", 200.0f },
            { "op2_ratio", 1.0f }, { "op2_level", 0.55f }, { "op2_wave", 0.2f },
            { "op2_decay", 300.0f }, { "op2_sustain", 0.25f }, { "op2_release", 150.0f },
            { "amp_attack", 1.0f }, { "amp_decay", 800.0f }, { "amp_sustain", 0.8f }, { "amp_release", 120.0f },
            { "drive", 0.35f }, { "flt_cutoff", 2500.0f }, { "flt_res", 0.25f }, { "level", -4.0f } } },

        { "Closed Hat",
          { { "fm_algo", 4 }, { "fm_feedback", 0.4f },
            { "op1_fixed", 1 }, { "op1_freq", 205.3f }, { "op1_wave", 1.0f }, { "op1_level", 0.5f },
            { "op2_fixed", 1 }, { "op2_freq", 369.6f }, { "op2_wave", 1.0f }, { "op2_level", 0.5f },
            { "op3_fixed", 1 }, { "op3_freq", 522.7f }, { "op3_wave", 1.0f }, { "op3_level", 0.5f },
            { "op4_fixed", 1 }, { "op4_freq", 800.0f }, { "op4_wave", 1.0f }, { "op4_level", 0.7f },
            { "amp_decay", 70.0f }, { "amp_release", 30.0f },
            { "flt_type", 2 }, { "flt_cutoff", 9000.0f }, { "flt_res", 0.4f },
            { "drive_type", 1 }, { "drive", 0.1f }, { "level", -1.0f } } },

        { "Open Hat",
          { { "fm_algo", 4 }, { "fm_feedback", 0.4f },
            { "op1_fixed", 1 }, { "op1_freq", 205.3f }, { "op1_wave", 1.0f }, { "op1_level", 0.5f },
            { "op2_fixed", 1 }, { "op2_freq", 369.6f }, { "op2_wave", 1.0f }, { "op2_level", 0.5f },
            { "op3_fixed", 1 }, { "op3_freq", 522.7f }, { "op3_wave", 1.0f }, { "op3_level", 0.5f },
            { "op4_fixed", 1 }, { "op4_freq", 800.0f }, { "op4_wave", 1.0f }, { "op4_level", 0.7f },
            { "amp_decay", 600.0f }, { "amp_release", 60.0f },
            { "flt_type", 2 }, { "flt_cutoff", 9000.0f }, { "flt_res", 0.4f },
            { "drive_type", 1 }, { "drive", 0.1f }, { "level", -3.0f } } },
    } };
    return voices;
}
} // namespace

float defaultVoiceValue (int voice, int param)
{
    const auto& key = voiceParamSpecs()[(size_t) param].key;

    for (const auto& [k, value] : defaultVoices()[(size_t) voice].values)
        if (key == k)
            return value;

    return voiceParamSpecs()[(size_t) param].def;
}

const char* defaultVoiceName (int voice)
{
    return defaultVoices()[(size_t) voice].name;
}

KitParams defaultKitParams()
{
    KitParams kit;

    for (int v = 0; v < kNumVoices; ++v)
    {
        for ([[maybe_unused]] const auto& [k, value] : defaultVoices()[(size_t) v].values)
            assert (findVoiceParam (k) >= 0);

        for (int p = 0; p < kNumVoiceParams; ++p)
            kit.voices[(size_t) v][p] = defaultVoiceValue (v, p);
    }

    for (int g = 0; g < kNumGlobalParams; ++g)
        kit.global[(size_t) g] = globalParamSpecs()[(size_t) g].def;

    return kit;
}

} // namespace batida
