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
        // Sine with a pitch sweep and a faint click.
        { "Kick",
          { { "fm_pitch", -29.4f }, { "fm_algo", 3 },
            { "op2_level", 0.18f }, { "op2_decay", 25.0f }, { "op2_sustain", 0.0f }, { "op2_release", 25.0f },
            { "pitch_amt", 26.0f }, { "pitch_decay", 160.0f },
            { "amp_decay", 520.0f }, { "amp_release", 50.0f },
            { "punch", 0.25f }, { "drive", 0.08f } } },

        // Two short sines (~500 Hz and ~1.7 kHz) plus a noise click.
        { "Rim",
          { { "fm_pitch", 11.2f }, { "fm_algo", 7 },
            { "op1_level", 0.55f }, { "op1_decay", 45.0f }, { "op1_sustain", 0.0f }, { "op1_release", 20.0f },
            { "op2_ratio", 3.4f }, { "op2_level", 0.45f }, { "op2_decay", 35.0f }, { "op2_sustain", 0.0f }, { "op2_release", 20.0f },
            { "op3_wave", 1.0f }, { "op3_level", 0.3f }, { "op3_decay", 12.0f }, { "op3_sustain", 0.0f }, { "op3_release", 10.0f },
            { "amp_decay", 60.0f }, { "amp_release", 30.0f },
            { "flt_type", 1 }, { "flt_cutoff", 300.0f }, { "flt_res", 0.1f }, { "level", 3.0f } } },

        // Two body sines plus a longer noise tail.
        { "Snare",
          { { "fm_pitch", -6.5f }, { "fm_algo", 7 },
            { "op1_level", 0.6f }, { "op1_decay", 140.0f }, { "op1_sustain", 0.0f }, { "op1_release", 40.0f },
            { "op2_ratio", 1.6f }, { "op2_level", 0.3f }, { "op2_decay", 90.0f }, { "op2_sustain", 0.0f }, { "op2_release", 30.0f },
            { "op3_wave", 1.0f }, { "op3_level", 0.9f }, { "op3_decay", 240.0f }, { "op3_sustain", 0.0f }, { "op3_release", 60.0f },
            { "pitch_amt", 7.0f }, { "pitch_decay", 40.0f },
            { "amp_decay", 280.0f }, { "amp_release", 60.0f },
            { "flt_cutoff", 12000.0f },
            { "punch", 0.35f }, { "drive", 0.12f }, { "level", -2.0f } } },

        // Band-passed noise with a two-stage decay: a snap, then a short tail.
        { "Clap",
          { { "fm_algo", 7 },
            { "op1_wave", 1.0f }, { "op1_level", 1.0f },
            { "amp_decay", 20.0f }, { "amp_sustain", 0.4f }, { "amp_release", 250.0f },
            { "flt_type", 2 }, { "flt_cutoff", 1200.0f }, { "flt_res", 0.35f },
            { "punch", 0.2f }, { "level", 6.0f } } },

        // Sine with a slow pitch drop and a little overtone.
        { "Tom",
          { { "fm_pitch", -15.0f }, { "fm_algo", 3 },
            { "op2_ratio", 1.5f }, { "op2_level", 0.12f }, { "op2_decay", 60.0f }, { "op2_sustain", 0.0f }, { "op2_release", 40.0f },
            { "pitch_amt", 9.0f }, { "pitch_decay", 220.0f },
            { "amp_decay", 600.0f }, { "amp_release", 60.0f },
            { "punch", 0.15f }, { "level", -2.0f } } },

        // Sine bass with a decaying modulator for the pluck; Gate mode, glide.
        { "Bass",
          { { "play_mode", 1 }, { "glide", 60.0f },
            { "fm_pitch", -24.0f }, { "fm_algo", 3 },
            { "op1_release", 200.0f },
            { "op2_ratio", 1.0f }, { "op2_level", 0.4f },
            { "op2_decay", 300.0f }, { "op2_sustain", 0.2f }, { "op2_release", 150.0f },
            { "amp_attack", 1.0f }, { "amp_decay", 800.0f }, { "amp_sustain", 0.8f }, { "amp_release", 120.0f },
            { "drive", 0.15f }, { "flt_cutoff", 2200.0f }, { "flt_res", 0.2f }, { "level", -4.0f } } },

        // Wide band-passed noise with a hint of two square tones for body.
        { "Closed Hat",
          { { "fm_algo", 7 },
            { "op1_wave", 1.0f }, { "op1_level", 1.0f },
            { "op2_fixed", 1 }, { "op2_freq", 540.0f }, { "op2_wave", 0.75f }, { "op2_level", 0.1f },
            { "op3_fixed", 1 }, { "op3_freq", 800.0f }, { "op3_wave", 0.75f }, { "op3_level", 0.1f },
            { "amp_decay", 55.0f }, { "amp_release", 25.0f },
            { "flt_type", 2 }, { "flt_cutoff", 9000.0f }, { "flt_res", 0.15f }, { "level", 5.0f } } },

        { "Open Hat",
          { { "fm_algo", 7 },
            { "op1_wave", 1.0f }, { "op1_level", 1.0f },
            { "op2_fixed", 1 }, { "op2_freq", 540.0f }, { "op2_wave", 0.75f }, { "op2_level", 0.1f },
            { "op3_fixed", 1 }, { "op3_freq", 800.0f }, { "op3_wave", 0.75f }, { "op3_level", 0.1f },
            { "amp_decay", 420.0f }, { "amp_release", 60.0f },
            { "flt_type", 2 }, { "flt_cutoff", 9000.0f }, { "flt_res", 0.15f }, { "level", 4.0f } } },
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
