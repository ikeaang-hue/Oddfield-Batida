#include "Parameters.h"

#include <cassert>

namespace batida
{

namespace
{
ParamSpec flt (std::string key, std::string label, float min, float max, float def,
               std::string unit = "", float centre = 0.0f, float step = 0.0f)
{
    ParamSpec s;
    s.key = std::move (key);
    s.label = std::move (label);
    s.min = min;
    s.max = max;
    s.def = def;
    s.unit = std::move (unit);
    s.centre = centre;
    s.step = step;
    return s;
}

ParamSpec choice (std::string key, std::string label, std::vector<std::string> choices, int def = 0)
{
    ParamSpec s;
    s.key = std::move (key);
    s.label = std::move (label);
    s.kind = Kind::Choice;
    s.min = 0.0f;
    s.max = (float) choices.size() - 1.0f;
    s.def = (float) def;
    s.step = 1.0f;
    s.choices = std::move (choices);
    return s;
}

ParamSpec toggle (std::string key, std::string label, bool def = false)
{
    ParamSpec s;
    s.key = std::move (key);
    s.label = std::move (label);
    s.kind = Kind::Bool;
    s.def = def ? 1.0f : 0.0f;
    s.step = 1.0f;
    return s;
}

std::vector<ParamSpec> buildVoiceSpecs()
{
    std::vector<ParamSpec> s (kNumVoiceParams);

    s[vp::SrcMode]  = choice ("src_mode", "Source", { "FM", "Sample", "Layer" });
    s[vp::Balance]  = flt ("balance", "Balance", 0.0f, 1.0f, 0.5f, "%");
    s[vp::Level]    = flt ("level", "Level", -60.0f, 6.0f, 0.0f, "dB", -12.0f, 0.1f);
    s[vp::Pan]      = flt ("pan", "Pan", -1.0f, 1.0f, 0.0f, "pan");
    s[vp::VelSens]  = flt ("vel_sens", "Velocity", 0.0f, 1.0f, 0.7f, "%");
    s[vp::PlayMode] = choice ("play_mode", "Play Mode", { "One-shot", "Gate" });
    s[vp::Glide]    = flt ("glide", "Glide", 0.0f, 2000.0f, 0.0f, "ms", 150.0f);

    s[vp::AmpA] = flt ("amp_attack", "Amp Attack", 0.0f, 2000.0f, 0.0f, "ms", 50.0f);
    s[vp::AmpD] = flt ("amp_decay", "Amp Decay", 5.0f, 8000.0f, 400.0f, "ms", 400.0f);
    s[vp::AmpS] = flt ("amp_sustain", "Amp Sustain", 0.0f, 1.0f, 0.0f, "%");
    s[vp::AmpR] = flt ("amp_release", "Amp Release", 5.0f, 8000.0f, 80.0f, "ms", 300.0f);

    s[vp::PitchAmt] = flt ("pitch_amt", "Pitch Env", -48.0f, 48.0f, 0.0f, "st", 0.0f, 0.01f);
    s[vp::PitchDec] = flt ("pitch_decay", "Pitch Decay", 2.0f, 2000.0f, 60.0f, "ms", 100.0f);

    s[vp::FmPitch]       = flt ("fm_pitch", "FM Pitch", -48.0f, 48.0f, 0.0f, "st", 0.0f, 0.01f);
    s[vp::FmAlgo]        = choice ("fm_algo", "Algorithm", { "1", "2", "3", "4", "5", "6", "7", "8" });
    s[vp::FmFeedback]    = flt ("fm_feedback", "Feedback", 0.0f, 1.0f, 0.0f, "%");
    s[vp::FmHarm]        = flt ("fm_harm", "Harmonic", -1.0f, 1.0f, 0.0f, "bi");
    s[vp::FmBright]      = flt ("fm_bright", "Brightness", -1.0f, 1.0f, 0.0f, "bi");
    s[vp::FmBrightDecay] = flt ("fm_bdecay", "Bright Decay", -1.0f, 1.0f, 0.0f, "bi");
    s[vp::FmGrit]        = flt ("fm_grit", "Grit", -1.0f, 1.0f, 0.0f, "bi");

    s[vp::SmpTune]      = flt ("smp_tune", "Sample Tune", -24.0f, 24.0f, 0.0f, "st", 0.0f, 0.01f);
    s[vp::SmpStart]     = flt ("smp_start", "Start", 0.0f, 1.0f, 0.0f, "%");
    s[vp::SmpEnd]       = flt ("smp_end", "End", 0.0f, 1.0f, 1.0f, "%");
    s[vp::SmpReverse]   = toggle ("smp_reverse", "Reverse");
    s[vp::SmpFadeIn]    = flt ("smp_fade_in", "Fade In", 0.0f, 1000.0f, 0.0f, "ms", 50.0f);
    s[vp::SmpFadeOut]   = flt ("smp_fade_out", "Fade Out", 0.0f, 2000.0f, 0.0f, "ms", 100.0f);
    s[vp::SmpGain]      = flt ("smp_gain", "Sample Gain", -24.0f, 24.0f, 0.0f, "dB", 0.0f, 0.1f);
    s[vp::SmpLoop]      = toggle ("smp_loop", "Loop");
    s[vp::SmpLoopStart] = flt ("smp_loop_start", "Loop Start", 0.0f, 1.0f, 0.0f, "%");
    s[vp::SmpLoopEnd]   = flt ("smp_loop_end", "Loop End", 0.0f, 1.0f, 1.0f, "%");
    s[vp::SmpSliceMode] = choice ("smp_slice_mode", "Slice Mode", { "Off", "Grid", "Transients" });
    s[vp::SmpSlices]    = flt ("smp_slices", "Slices", 2.0f, 32.0f, 8.0f, "n", 0.0f, 1.0f);
    s[vp::SmpSliceSens] = flt ("smp_slice_sens", "Slice Sensitivity", 0.0f, 1.0f, 0.5f, "%");

    s[vp::Punch]     = flt ("punch", "Punch", 0.0f, 1.0f, 0.0f, "%");
    s[vp::Drive]     = flt ("drive", "Drive", 0.0f, 1.0f, 0.0f, "%");
    s[vp::DriveType] = choice ("drive_type", "Drive Type", { "Soft", "Hard", "Fold", "Crush" });
    s[vp::FltType]   = choice ("flt_type", "Filter", { "Low-pass", "High-pass", "Band-pass" });
    s[vp::FltCutoff] = flt ("flt_cutoff", "Cutoff", 20.0f, 20000.0f, 20000.0f, "Hz", 1000.0f);
    s[vp::FltRes]    = flt ("flt_res", "Resonance", 0.0f, 1.0f, 0.0f, "%");

    s[vp::ChainAmt] = flt ("chain_amt", "Chain", 0.0f, 1.0f, 1.0f, "%");
    s[vp::Mute]     = toggle ("mute", "Mute");
    s[vp::Solo]     = toggle ("solo", "Solo");
    s[vp::Choke]    = choice ("choke", "Choke", { "Off", "1", "2", "3", "4" });

    for (int o = 0; o < kNumOps; ++o)
    {
        const auto k = "op" + std::to_string (o + 1) + "_";
        const auto l = "Op" + std::to_string (o + 1) + " ";

        s[opParam (o, Ratio)]   = flt (k + "ratio", l + "Ratio", 0.125f, 32.0f, 1.0f, "x", 2.0f, 0.001f);
        s[opParam (o, Fixed)]   = toggle (k + "fixed", l + "Fixed");
        s[opParam (o, Freq)]    = flt (k + "freq", l + "Freq", 10.0f, 20000.0f, 1000.0f, "Hz", 1000.0f);
        s[opParam (o, OpLevel)] = flt (k + "level", l + "Level", 0.0f, 1.0f, o == 0 ? 1.0f : 0.0f, "%");
        s[opParam (o, Wave)]    = flt (k + "wave", l + "Wave", 0.0f, 1.0f, 0.0f, "wave");
        s[opParam (o, Fold)]    = flt (k + "fold", l + "Fold", 0.0f, 1.0f, 0.0f, "%");
        s[opParam (o, OpA)]     = flt (k + "attack", l + "Attack", 0.0f, 2000.0f, 0.0f, "ms", 50.0f);
        s[opParam (o, OpD)]     = flt (k + "decay", l + "Decay", 5.0f, 8000.0f, 600.0f, "ms", 400.0f);
        s[opParam (o, OpS)]     = flt (k + "sustain", l + "Sustain", 0.0f, 1.0f, 1.0f, "%");
        s[opParam (o, OpR)]     = flt (k + "release", l + "Release", 5.0f, 8000.0f, 1500.0f, "ms", 300.0f);
    }

    for ([[maybe_unused]] const auto& spec : s)
        assert (! spec.key.empty());

    return s;
}

std::vector<ParamSpec> buildGlobalSpecs()
{
    std::vector<ParamSpec> s (kNumGlobalParams);
    s[gp::MidiMode] = choice ("midi_mode", "MIDI Mode", { "Drum map", "Chromatic" });
    s[gp::Master]   = flt ("master", "Master", -60.0f, 6.0f, -8.0f, "dB", -12.0f, 0.1f); // headroom: overlapping hits sum hot
    s[gp::KeysVoice] = choice ("keys_voice", "Keys Sound", { "1", "2", "3", "4", "5", "6", "7", "8" });

    s[gp::XyX] = flt ("xy_x", "XY Character", 0.0f, 1.0f, 0.5f, "%");
    s[gp::XyY] = flt ("xy_y", "XY Heat", 0.0f, 1.0f, 0.2f, "%");

    s[gp::CompAmount]   = flt ("comp_amount", "Comp Amount", 0.0f, 1.0f, 0.0f, "%");
    s[gp::CompAttack]   = flt ("comp_attack", "Comp Attack", 0.1f, 100.0f, 10.0f, "ms", 10.0f);
    s[gp::CompRelease]  = flt ("comp_release", "Comp Release", 10.0f, 1000.0f, 120.0f, "ms", 120.0f);
    s[gp::CompMix]      = flt ("comp_mix", "Comp Mix", 0.0f, 1.0f, 1.0f, "%");
    s[gp::CompDetector] = choice ("comp_detector", "Comp Detector", { "Internal", "Sidechain" });
    s[gp::CompFollow]   = toggle ("comp_follow", "Comp Follow XY", true);

    s[gp::DistDrive]   = flt ("dist_drive", "Dist Drive", 0.0f, 1.0f, 0.0f, "%");
    s[gp::DistType]    = flt ("dist_type", "Dist Type", 0.0f, 1.0f, 0.5f, "dist");
    s[gp::DistLowKeep] = flt ("dist_low_keep", "Low Keep", 40.0f, 300.0f, 90.0f, "Hz", 100.0f);
    s[gp::ExcAmount]   = flt ("exc_amount", "Exciter", 0.0f, 1.0f, 0.0f, "%");
    s[gp::ExcTone]     = flt ("exc_tone", "Exciter Tone", 2000.0f, 12000.0f, 5000.0f, "Hz", 5000.0f);
    s[gp::DistFollow]  = toggle ("dist_follow", "Dist Follow XY", true);

    s[gp::EqHp]     = flt ("eq_hp", "EQ High-pass", 20.0f, 1000.0f, 20.0f, "Hz", 100.0f);
    s[gp::EqLp]     = flt ("eq_lp", "EQ Low-pass", 1000.0f, 20000.0f, 20000.0f, "Hz", 6000.0f);
    s[gp::EqLow]    = flt ("eq_low", "EQ Low", -12.0f, 12.0f, 0.0f, "dB", 0.0f, 0.1f);
    s[gp::EqMid]    = flt ("eq_mid", "EQ Mid", -12.0f, 12.0f, 0.0f, "dB", 0.0f, 0.1f);
    s[gp::EqHigh]   = flt ("eq_high", "EQ High", -12.0f, 12.0f, 0.0f, "dB", 0.0f, 0.1f);
    s[gp::EqFollow] = toggle ("eq_follow", "EQ Follow XY", true);

    s[gp::ChainOut]   = flt ("chain_out", "Chain Out", -24.0f, 12.0f, 0.0f, "dB", 0.0f, 0.1f);
    s[gp::SafetyClip] = toggle ("safety_clip", "Safety Clip", true);

    s[gp::SeqPattern]  = choice ("seq_pattern", "Pattern", { "1", "2", "3", "4", "5", "6", "7", "8",
                                                             "9", "10", "11", "12", "13", "14", "15", "16" });
    s[gp::SeqRun]      = choice ("seq_run", "Run Mode", { "Keys", "Transport" });
    s[gp::SeqPlay]     = toggle ("seq_play", "Play");
    s[gp::SeqTempo]    = flt ("seq_tempo", "Tempo", 40.0f, 240.0f, 120.0f, "bpm", 0.0f, 0.01f);
    s[gp::SeqSwing]    = flt ("seq_swing", "Swing", 0.5f, 0.75f, 0.5f, "%");
    s[gp::SeqQuantise] = choice ("seq_quantise", "Quantise", { "Step", "Beat", "Bar" }, 1);
    s[gp::SeqLatch]    = toggle ("seq_latch", "Latch");
    s[gp::SeqSync]     = toggle ("seq_sync", "Sync", true); // follow the host's tempo and position

    for (int m = 0; m < kNumMods; ++m)
    {
        const auto k = "mod" + std::to_string (m + 1) + "_";
        const auto l = "Mod" + std::to_string (m + 1) + " ";
        s[modParam (m, ModMode)]     = choice (k + "mode", l + "Mode", { "Sync", "Free", "One-shot" });
        s[modParam (m, ModRate)]     = choice (k + "rate", l + "Rate", { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
                                                                          "1/4T", "1/4", "1/4D", "1/2", "1 bar", "2 bars",
                                                                          "4 bars", "8 bars" }, 11);
        s[modParam (m, ModHz)]       = flt (k + "hz", l + "Rate Hz", 0.01f, 20.0f, 1.0f, "Hz", 1.0f);
        s[modParam (m, ModPolarity)] = choice (k + "polarity", l + "Polarity", { "Unipolar", "Bipolar" });
        s[modParam (m, ModSmooth)]   = flt (k + "smooth", l + "Smooth", 0.0f, 1.0f, 0.0f, "%");
        s[modParam (m, ModHuman)]    = flt (k + "humanise", l + "Humanise", 0.0f, 1.0f, 0.0f, "%");
        s[modParam (m, ModAmount)]   = flt (k + "amount", l + "Amount", 0.0f, 1.0f, 1.0f, "%");
        s[modParam (m, ModTrigger)]  = choice (k + "trigger", l + "Trigger", { "Any hit", "Pattern start", "Sound 1", "Sound 2",
                                                                               "Sound 3", "Sound 4", "Sound 5", "Sound 6",
                                                                               "Sound 7", "Sound 8" });
    }

    s[gp::SceneA]       = choice ("scene_a", "Scene Morph From", { "A", "B", "C", "D" }, 0);
    s[gp::SceneB]       = choice ("scene_b", "Scene Morph To", { "A", "B", "C", "D" }, 1);
    s[gp::SceneMorph]   = flt ("scene_morph", "Scene Morph", 0.0f, 1.0f, 0.0f, "%");
    s[gp::SceneMorphOn] = toggle ("scene_morph_on", "Scene Morph On");
    return s;
}
} // namespace

const std::vector<ParamSpec>& voiceParamSpecs()
{
    static const auto specs = buildVoiceSpecs();
    return specs;
}

const std::vector<ParamSpec>& globalParamSpecs()
{
    static const auto specs = buildGlobalSpecs();
    return specs;
}

std::string voiceParamID (int voice, int param)
{
    return "v" + std::to_string (voice + 1) + "_" + voiceParamSpecs()[(size_t) param].key;
}

std::string voiceParamName (int voice, int param)
{
    return "S" + std::to_string (voice + 1) + " " + voiceParamSpecs()[(size_t) param].label; // "S" for sound
}

std::string globalParamID (int param)
{
    return globalParamSpecs()[(size_t) param].key;
}

int findVoiceParam (const std::string& key)
{
    const auto& specs = voiceParamSpecs();
    for (size_t i = 0; i < specs.size(); ++i)
        if (specs[i].key == key)
            return (int) i;
    return -1;
}

} // namespace batida
