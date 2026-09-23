#pragma once

#include <array>
#include <string>
#include <vector>

// Every parameter the engine understands, defined in one place. The plugin
// builds its host parameters from these tables; the engine reads plain floats.
//
// IDs are fixed from phase 1 on: adding parameters later is safe, renaming or
// removing one breaks saved automation.

namespace batida
{

constexpr int kNumVoices = 8;
constexpr int kNumOps = 4;
constexpr int kNumAlgorithms = 8;

enum class Kind { Float, Choice, Bool };

struct ParamSpec
{
    std::string key;     // ID suffix, e.g. "amp_decay" or "op2_ratio"
    std::string label;   // display suffix, e.g. "Amp Decay"
    Kind kind = Kind::Float;
    float min = 0.0f, max = 1.0f, def = 0.0f;
    float centre = 0.0f; // value shown at mid-knob (skew); 0 = linear
    float step = 0.0f;   // 0 = continuous
    std::string unit;    // "ms", "Hz", "st", "dB", "%" or ""
    std::vector<std::string> choices;
};

// Voice parameter indices: general parameters, then 4 operators × OpField.
namespace vp
{
enum : int
{
    SrcMode, Balance, Level, Pan, VelSens, PlayMode, Glide,
    AmpA, AmpD, AmpS, AmpR,
    PitchAmt, PitchDec,
    FmPitch, FmAlgo, FmFeedback, FmHarm, FmBright, FmBrightDecay, FmGrit,
    SmpTune, SmpStart, SmpEnd, SmpReverse, SmpFadeIn, SmpFadeOut, SmpGain,
    SmpLoop, SmpLoopStart, SmpLoopEnd,
    SmpSliceMode, SmpSlices, SmpSliceSens,
    Punch, Drive, DriveType, FltType, FltCutoff, FltRes,
    ChainAmt, // dry/wet into the kit chain
    OpBase
};
}

enum OpField : int { Ratio, Fixed, Freq, OpLevel, Wave, Fold, OpA, OpD, OpS, OpR, kNumOpFields };

constexpr int opParam (int op, int field) { return vp::OpBase + op * kNumOpFields + field; }
constexpr int kNumVoiceParams = vp::OpBase + kNumOps * kNumOpFields;

namespace gp
{
enum : int
{
    MidiMode, Master, KeysVoice,
    // Kit chain (phase 2)
    XyX, XyY,
    CompAmount, CompAttack, CompRelease, CompMix, CompDetector, CompFollow,
    DistDrive, DistType, DistLowKeep, ExcAmount, ExcTone, DistFollow,
    EqHp, EqLp, EqLow, EqMid, EqHigh, EqFollow,
    ChainOut, SafetyClip,
    // Sequencer (phase 3)
    SeqPattern, SeqRun, SeqPlay, SeqTempo, SeqSwing, SeqQuantise, SeqLatch,
    SeqSync,
    Count
};
}
constexpr int kNumGlobalParams = gp::Count;

enum class SourceMode { FM, Sample, Layer };
enum class PlayMode { OneShot, Gate };
enum class DriveType { Soft, Hard, Fold, Crush };
enum class FilterType { LowPass, HighPass, BandPass };
enum class MidiMode { DrumMap, Chromatic };
enum class Detector { Internal, Sidechain };
enum class SliceMode { Off, Grid, Transients };

const std::vector<ParamSpec>& voiceParamSpecs();  // kNumVoiceParams entries, in index order
const std::vector<ParamSpec>& globalParamSpecs(); // kNumGlobalParams entries

std::string voiceParamID (int voice, int param);   // "v1_amp_decay" (voice is 0-based)
std::string voiceParamName (int voice, int param); // "V1 Amp Decay"
std::string globalParamID (int param);             // "midi_mode"
int findVoiceParam (const std::string& key);       // index for a key, or -1

struct VoiceParams
{
    std::array<float, kNumVoiceParams> v {};

    float operator[] (int i) const { return v[(size_t) i]; }
    float& operator[] (int i) { return v[(size_t) i]; }
    float op (int o, int field) const { return v[(size_t) opParam (o, field)]; }
    int choice (int i) const { return (int) (v[(size_t) i] + 0.5f); }
    bool flag (int i) const { return v[(size_t) i] > 0.5f; }
};

struct KitParams
{
    std::array<VoiceParams, kNumVoices> voices {};
    std::array<float, kNumGlobalParams> global {};
};

// The default kit: each slot starts with a usable FM sound (DefaultKit.cpp).
float defaultVoiceValue (int voice, int param);
const char* defaultVoiceName (int voice);
KitParams defaultKitParams();

} // namespace batida
