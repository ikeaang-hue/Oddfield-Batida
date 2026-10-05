#include "Reference.h"

#include "Engine/Movement/Movement.h"
#include "Engine/Sequencer/Pattern.h"

#include <cstdio>

namespace batida
{

namespace
{
juce::String number (float v)
{
    char buffer[32];
    std::snprintf (buffer, sizeof (buffer), "%g", (double) v);
    return buffer;
}

// A float as JSON shows it best: 0.1, not 0.100000001490116.
juce::var plain (float v)
{
    return number (v).getDoubleValue();
}

juce::String choiceList (const ParamSpec& spec)
{
    juce::StringArray c;
    for (const auto& s : spec.choices)
        c.add (s);
    return c.joinIntoString (", ");
}

juce::String rangeCell (const ParamSpec& spec)
{
    if (spec.kind == Kind::Choice)
        return choiceList (spec);
    if (spec.kind == Kind::Bool)
        return "0 or 1";
    const auto unit = unitLabel (spec);
    return number (spec.min) + ".." + number (spec.max) + (unit.isNotEmpty() ? " " + unit : juce::String());
}

juce::String defaultCell (const ParamSpec& spec)
{
    if (spec.kind == Kind::Choice)
        return spec.choices[(size_t) std::clamp ((int) (spec.def + 0.5f), 0, (int) spec.choices.size() - 1)];
    return number (spec.def);
}

juce::String row (const ParamSpec& spec, const juce::String& key, const juce::String& meaning, const juce::String& def = {})
{
    return "| `" + key + "` | " + rangeCell (spec) + " | " + (def.isNotEmpty() ? def : defaultCell (spec)) + " | " + meaning + " |\n";
}

const char* kTableHead = "| Setting | Range | Default | What it does |\n|---|---|---|---|\n";

bool isModField (int g) { return g >= gp::ModBase && g < gp::ModBase + kNumMods * kNumModFields; }
} // namespace

juce::String unitLabel (const ParamSpec& spec)
{
    const auto& u = spec.unit;
    if (u == "st") return "semitones";
    if (u == "ct") return "cents";
    if (u == "x") return "x the note";
    if (u == "n") return "slices";
    if (u == "ms" || u == "Hz" || u == "dB" || u == "bpm") return u;
    return {}; // fractions (shown as % in the plugin), -1..1 macros, pan, wave and type blends
}

juce::String describeVoiceParam (int p)
{
    if (p >= vp::OpBase)
    {
        switch ((p - vp::OpBase) % kNumOpFields)
        {
            case Ratio:   return "Frequency as a multiple of the note (1 = the note, 2 = an octave up). Ignored when Fixed is on.";
            case Fixed:   return "On: the operator ignores the note and runs at its Freq.";
            case Freq:    return "The fixed frequency, when Fixed is on.";
            case OpLevel: return "A carrier's output level, or a modulator's modulation depth (which carriers and modulators are depends on fm_algo).";
            case Wave:    return "Waveform morph: 0 sine, then triangle, saw and square; 1 is white noise, which ignores pitch (hats, snares, claps).";
            case Fold:    return "Wavefolding: bends the waveform back on itself for brighter, harder overtones.";
            case OpA:     return "The operator's envelope attack.";
            case OpD:     return "The operator's envelope decay.";
            case OpS:     return "The operator's envelope sustain level.";
            case OpR:     return "The operator's envelope release.";
            default:      break;
        }
        return {};
    }
    switch (p)
    {
        case vp::SrcMode:       return "What makes the sound: FM (four operators), Sample, or Layer (both, mixed by balance).";
        case vp::Balance:       return "Layer only: 0 = only the sample, 0.5 = both at full level, 1 = only FM.";
        case vp::Level:         return "The sound's level.";
        case vp::Pan:           return "-1 left, 0 centre, 1 right.";
        case vp::VelSens:       return "How much velocity changes the level: 0 = not at all, 1 = fully.";
        case vp::PlayMode:      return "One-shot plays the whole envelope from a hit; Gate holds while the note (or the step's noteLength) lasts.";
        case vp::Glide:         return "Time to slide from the previous note's pitch (bass lines and slides).";
        case vp::AmpA:          return "Amp envelope attack.";
        case vp::AmpD:          return "Amp envelope decay: for a one-shot, how long the hit rings.";
        case vp::AmpS:          return "Amp envelope sustain level (Gate mode holds here).";
        case vp::AmpR:          return "Amp envelope release, after the note ends.";
        case vp::PitchAmt:      return "Pitch envelope depth: the hit starts this far above (or below) its pitch and falls back. +20..+40 makes a kick's drop.";
        case vp::PitchDec:      return "How fast the pitch envelope falls back.";
        case vp::FmPitch:       return "Transposes the FM source. With op ratio 1 the note 60 (C3) plays at 261.6 Hz; -24 puts it at 65.4 Hz (a kick or bass).";
        case vp::FmAlgo:        return "Operator routing (see Algorithms). Op 4 carries the feedback in every algorithm.";
        case vp::FmFeedback:    return "Op 4 feeding back into itself: from brighter to noisy.";
        case vp::FmHarm:        return "Macro: 0 leaves the modulators' ratios as set; towards -1 more harmonic, towards 1 more inharmonic (bells, metal).";
        case vp::FmBright:      return "Macro: scales every modulator's depth (x0.5 at -1, x2 at 1). Brighter or darker.";
        case vp::FmBrightDecay: return "Macro: scales the modulators' decay and release (x4 longer at -1, x4 shorter at 1): how fast the brightness fades.";
        case vp::FmGrit:        return "Macro: added to fm_feedback.";
        case vp::SmpTune:       return "Sample pitch. It changes speed (no time-stretching).";
        case vp::SmpStart:      return "Where the sample starts, as a fraction of its length.";
        case vp::SmpEnd:        return "Where the sample ends, as a fraction of its length.";
        case vp::SmpReverse:    return "Plays the sample backwards.";
        case vp::SmpFadeIn:     return "Fade in from the start point.";
        case vp::SmpFadeOut:    return "Fade out before the end point.";
        case vp::SmpGain:       return "The sample's gain before the sound's FX.";
        case vp::SmpLoop:       return "Gate mode: loops between the loop points while the note is held.";
        case vp::SmpLoopStart:  return "Loop start, as a fraction of the sample's length.";
        case vp::SmpLoopEnd:    return "Loop end, as a fraction of the sample's length.";
        case vp::SmpSliceMode:  return "Off, Grid (even slices) or Transients (slices start at the hits). A pattern step's slice lane picks which slice plays.";
        case vp::SmpSlices:     return "How many slices (Grid), or the most (Transients).";
        case vp::SmpSliceSens:  return "Transients: how weak a hit can be and still start a slice.";
        case vp::Punch:         return "Transient and compression in one control: more snap and body.";
        case vp::Drive:         return "Distortion amount (level compensated).";
        case vp::DriveType:     return "Soft (warm), Hard (clipped), Fold (wavefolded) or Crush (bit and rate reduced).";
        case vp::FltType:       return "The sound's filter: Low-pass, High-pass or Band-pass.";
        case vp::FltCutoff:     return "Filter cutoff. 20000 on the low-pass is open.";
        case vp::FltRes:        return "Filter resonance.";
        case vp::ChainAmt:      return "How much of the sound goes through the kit chain: 0 = dry (bypasses it), 1 = all through it.";
        case vp::Mute:          return "Mutes the sound.";
        case vp::Solo:          return "Solos the sound.";
        case vp::Choke:         return "Off, or a group 1-4: a hit cuts the other sounding sounds in its group (open and closed hats).";
        case vp::StackCount:    return "How many copies of the source play (thickness, or a chord with stack_interval).";
        case vp::StackDetune:   return "The copies spread evenly across plus and minus this many cents.";
        case vp::StackSpread:   return "The copies spread across the stereo field: 0 = all centred, 1 = full width.";
        case vp::StackInterval: return "The copies' notes, in order: Unison, Octave (0 12 0 12), Fifth, Minor (0 3 7 12), Minor 7 (0 3 7 10), Major.";
        default:                break;
    }
    return {};
}

juce::String describeGlobalParam (int g)
{
    if (isModField (g))
    {
        switch ((g - gp::ModBase) % kNumModFields)
        {
            case ModMode:     return "Sync loops the shape in time with the tempo; Free runs at modN_hz; One-shot plays it once per trigger.";
            case ModRate:     return "Sync and One-shot: how long one pass of the shape takes.";
            case ModHz:       return "Free: cycles per second.";
            case ModPolarity: return "Unipolar moves targets one way from where they're set; Bipolar moves them both ways.";
            case ModSmooth:   return "Rounds the shape's corners.";
            case ModHuman:    return "Random variation per cycle.";
            case ModAmount:   return "Scales every target's depth.";
            case ModTrigger:  return "What restarts the shape: any hit, the pattern's start, or one sound's hits.";
            default:          break;
        }
        return {};
    }
    switch (g)
    {
        case gp::MidiMode:     return "Drum map: notes 36-43 play slots 1-8 on any channel. Chromatic: every channel but 10 plays keys_voice across the keyboard.";
        case gp::Master:       return "Batida's output level (headroom by default, since hits add up).";
        case gp::KeysVoice:    return "The sound that Chromatic mode plays.";
        case gp::XyX:          return "The pad's X, character: warm (0) through aggressive to digital (1). Moves the stages that follow XY.";
        case gp::XyY:          return "The pad's Y, heat: clean (0) to destroyed (1). Crushes more than it squashes, and stays about as loud.";
        case gp::CompAmount:   return "Kit compressor: how much. Make-up gain is automatic, so this changes density, not level.";
        case gp::CompAttack:   return "Kit compressor attack.";
        case gp::CompRelease:  return "Kit compressor release.";
        case gp::CompMix:      return "Kit compressor dry/wet (parallel compression).";
        case gp::CompDetector: return "What the compressor listens to: its own input, or the plugin's sidechain input from the DAW.";
        case gp::CompFollow:   return "The compressor follows the XY pad.";
        case gp::DistDrive:    return "Kit distortion amount (level compensated).";
        case gp::DistType:     return "Distortion character, blended: 0 warm (tape, tube), 0.5 aggressive (clip, fold), 1 digital (crush, decimate).";
        case gp::DistLowKeep:  return "Below this frequency the signal skips the distortion, so the sub stays clean.";
        case gp::ExcAmount:    return "Exciter: adds high harmonics.";
        case gp::ExcTone:      return "Where the exciter starts.";
        case gp::DistFollow:   return "The distortion and exciter follow the XY pad.";
        case gp::EqHp:         return "Kit high-pass.";
        case gp::EqLp:         return "Kit low-pass (20000 is open).";
        case gp::EqLow:        return "Low shelf.";
        case gp::EqMid:        return "Mid band.";
        case gp::EqHigh:       return "High shelf.";
        case gp::EqFollow:     return "The EQ follows the XY pad.";
        case gp::ChainOut:     return "The kit chain's output level.";
        case gp::SafetyClip:   return "A soft ceiling just under 0 dBFS at the very end.";
        case gp::SeqPattern:   return "The pattern that plays (and the one the SEQ page shows).";
        case gp::SeqRun:       return "Keys: patterns play while their key (60-75) is held. Transport: the pattern runs with the host's play (or seq_play).";
        case gp::SeqPlay:      return "Transport mode: plays with the host stopped.";
        case gp::SeqTempo:     return "The tempo when not following the host (seq_sync off, or the host stopped).";
        case gp::SeqSwing:     return "0.5 straight, up to 0.75 (heavy shuffle). Delays every second 16th.";
        case gp::SeqQuantise:  return "When a pattern key's pattern starts: the next step, beat or bar.";
        case gp::SeqLatch:     return "A pattern key keeps its pattern playing after release.";
        case gp::SeqSync:      return "Follow the host's tempo and position.";
        case gp::SceneA:       return "The scene the morph starts from.";
        case gp::SceneB:       return "The scene the morph goes to.";
        case gp::SceneMorph:   return "0 = scene_a, 1 = scene_b, in between blended.";
        case gp::SceneMorphOn: return "The morph drives the chain (off: the chain stays where its controls are).";
        default:               break;
    }
    return {};
}

juce::String exampleFile (PresetType type)
{
    const juce::String header = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    switch (type)
    {
        case PresetType::Sound:
            return header + R"(<BATIDA type="sound">
  <INFO name="Deep Kick" category="kick" tags="round"/>
  <VOICE src_mode="FM" fm_pitch="-24" amp_decay="450" pitch_amt="30" pitch_decay="45"
         punch="0.4" drive="0.2" drive_type="soft"/>
</BATIDA>
)";
        case PresetType::Kit:
            return header + R"(<BATIDA type="kit">
  <INFO name="Two Hits" tags="dark"/>
  <KIT xy_x="0.3" xy_y="0.5">
    <VOICE index="0" name="Kick" fm_pitch="-24" amp_decay="400" pitch_amt="28" pitch_decay="40"/>
    <VOICE index="2" name="Snare" op1_wave="1" amp_decay="180" flt_type="band-pass"
           flt_cutoff="2500" flt_res="0.3"/>
  </KIT>
</BATIDA>
)";
        case PresetType::Pattern:
            return header + R"(<BATIDA type="pattern">
  <INFO name="Four Floor"/>
  <PATTERN length="16">
    <TRACK voice="0" gate="x... x... x... x..."/>
    <TRACK voice="2" gate=".... x... .... x..." velocity="100,100,100,100,120"/>
    <TRACK voice="6" gate="..x. ..x. ..x. ..x." probability="100,100,80"/>
  </PATTERN>
</BATIDA>
)";
        case PresetType::Set:
            return header + R"(<BATIDA type="set">
  <INFO name="Sketch"/>
  <KIT seq_tempo="128">
    <VOICE index="0" name="Kick" fm_pitch="-24" amp_decay="400" pitch_amt="28" pitch_decay="40"/>
  </KIT>
  <PATTERNS>
    <PATTERN index="0" length="16">
      <TRACK voice="0" gate="x... x... x... x..."/>
    </PATTERN>
  </PATTERNS>
</BATIDA>
)";
    }
    return {};
}

juce::var parameterData()
{
    juce::Array<juce::var> list;
    auto add = [&] (const ParamSpec& spec, const juce::String& element, const juce::String& hostId,
                    const juce::String& description, const juce::String& files)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("key", juce::String (spec.key));
        o->setProperty ("element", element);
        o->setProperty ("files", files);
        o->setProperty ("hostId", hostId);
        o->setProperty ("label", juce::String (spec.label));
        o->setProperty ("kind", spec.kind == Kind::Choice ? "menu" : spec.kind == Kind::Bool ? "switch" : "number");
        o->setProperty ("min", plain (spec.min));
        o->setProperty ("max", plain (spec.max));
        o->setProperty ("default", spec.kind == Kind::Choice ? juce::var (defaultCell (spec)) : plain (spec.def));
        if (spec.kind == Kind::Float)
        {
            o->setProperty ("unit", unitLabel (spec));
            if (spec.step > 0.0f)
                o->setProperty ("step", plain (spec.step));
            if (spec.centre > spec.min && spec.centre < spec.max && spec.centre != 0.5f * (spec.min + spec.max))
                o->setProperty ("centre", plain (spec.centre));
        }
        if (spec.kind == Kind::Choice)
        {
            juce::Array<juce::var> choices;
            for (const auto& c : spec.choices)
                choices.add (juce::String (c));
            o->setProperty ("choices", choices);
        }
        o->setProperty ("description", description);
        list.add (juce::var (o));
    };
    for (int p = 0; p < kNumVoiceParams; ++p)
    {
        const auto& spec = voiceParamSpecs()[(size_t) p];
        add (spec, "VOICE", "v1_" + juce::String (spec.key) + " .. v8_" + juce::String (spec.key), describeVoiceParam (p), "sound, kit, set");
    }
    for (int g = 0; g < kNumGlobalParams; ++g)
    {
        const auto& spec = globalParamSpecs()[(size_t) g];
        add (spec, "KIT", juce::String (spec.key), describeGlobalParam (g), isKitGlobal (g) ? "kit, set" : "set");
    }
    return list;
}

juce::String referenceMarkdown()
{
    juce::String md;
    auto voiceRows = [&] (int from, int to)
    {
        md << kTableHead;
        for (int p = from; p <= to; ++p)
            md << row (voiceParamSpecs()[(size_t) p], voiceParamSpecs()[(size_t) p].key, describeVoiceParam (p));
        md << "\n";
    };
    auto globalRows = [&] (std::initializer_list<std::pair<int, int>> ranges)
    {
        md << kTableHead;
        for (const auto& [from, to] : ranges)
            for (int g = from; g <= to; ++g)
                md << row (globalParamSpecs()[(size_t) g], globalParamID (g), describeGlobalParam (g));
        md << "\n";
    };

    md << "# Batida files and settings\n\n"
          "Everything Batida saves is a plain XML file: a **sound**, a **kit**, a **pattern** or a **set**. This page lists "
          "every element and setting in them, so a file can be written or edited by hand or by a script. It's generated "
          "from Batida's own parameter tables. `batida params` prints the same data for the version that's installed, "
          "and `batida validate` checks a file against it.\n\n";

    md << "## Files\n\n"
          "| Level | File | Holds | Folder in the library |\n|---|---|---|---|\n"
          "| Sound | `.batida-sound` | one slot's sound (`VOICE`) | `User/Sounds/<Category>/` |\n"
          "| Kit | `.batida-kit` | 8 sounds, the kit chain, the pad, modulators and scenes (`KIT`) | `User/Kits/` |\n"
          "| Pattern | `.batida-pattern` | one pattern (`PATTERN`) | `User/Patterns/` |\n"
          "| Set | `.batida-set` | everything: the kit, all 16 patterns, the sequencer, MIDI and master (`KIT`, `PATTERNS`) | `User/Sets/` |\n\n"
          << juce::String::fromUTF8 ("The library is `~/Music/Oddfield/Batida/`. A file saved into its `User` folder shows up in LIB and the "
                                     "◀ name ▶ strips within a few seconds while a Batida window is open. When the file behind the loaded "
                                     "sound, kit or pattern changes, its strip shows a lime ↻: click it to load the new version (one undo step).\n\n");

    md << "## The smallest files\n\n"
          "Only what differs from the defaults needs to be written.\n\n";
    for (auto [type, title] : { std::pair { PresetType::Sound, "A sound" }, std::pair { PresetType::Kit, "A kit" },
                                std::pair { PresetType::Pattern, "A pattern" }, std::pair { PresetType::Set, "A set" } })
        md << "**" << title << "**\n\n```xml\n" << exampleFile (type) << "```\n\n";

    md << "## How values are read\n\n"
          "- **Units:** numbers are in the unit the tables give (ms, Hz, dB, semitones...). Amounts without a unit are "
          "fractions: 0.5 is what the plugin shows as 50%.\n"
          "- **Out of range:** a number outside its range is clamped.\n"
          "- **Menus:** by name, in any case (`drive_type=\"soft\"`), or by index from 0.\n"
          "- **Switches:** 1 or 0, true or false, on or off, yes or no.\n"
          "- **Missing settings** take their defaults. A `VOICE` starts from the default sound (the Default column), "
          "not from the slot's sound in the Neutral kit; a kit's slots with no `VOICE` keep the Neutral kit's sounds.\n"
          "- **Slots and patterns count from 0** in files: `VOICE index=\"0\"` and `TRACK voice=\"0\"` are slot 1, "
          "`PATTERN index=\"0\"` is pattern 1.\n"
          "- **Unknown elements and settings** are skipped. `batida validate` lists them, with the nearest known name.\n"
          "- `<BATIDA type=\"...\">` must match the file's extension. `format` (default 1) and `madeWith` are optional; "
          "a file from a newer format still opens, and what this version knows is read.\n\n";

    md << "## INFO\n\n"
          "`<INFO name=\"...\" author=\"...\" category=\"...\" tags=\"...\"/>`, all optional. Without a name, the file's "
          "name is used. **category** (sounds): one of " << soundCategories().joinIntoString (", ") << ". **tags**: "
          "comma-separated words; the browser offers " << characterTags().joinIntoString (", ") << ", and any other "
          "word works as a tag too.\n\n";

    md << "## Sound settings\n\n"
          "Attributes of `VOICE`: the whole element in a sound file, and each `KIT > VOICE` in kits and sets (which also "
          "take `index`, 0..7, and `name`). The same settings are the host parameters `v1_<key>` .. `v8_<key>`.\n\n";
    md << "### Source and play\n\n";
    voiceRows (vp::SrcMode, vp::Glide);
    md << "### Envelopes\n\n";
    voiceRows (vp::AmpA, vp::PitchDec);
    md << "### FM\n\n";
    voiceRows (vp::FmPitch, vp::FmGrit);
    md << "**Algorithms** (`fm_algo`; `a > b` means a modulates b, and the operators on the right are heard):\n\n"
          "| # | Routing |\n|---|---|\n"
          "| 1 | 4 > 3 > 2 > 1 |\n| 2 | (3 + 4) > 2 > 1 |\n| 3 | (4 > 3) + 2 > 1 |\n| 4 | 4 > 3, 2 > 1 (1 and 3 heard) |\n"
          "| 5 | 4 > (1, 2, 3) |\n| 6 | 4 > 3; 1, 2, 3 heard |\n| 7 | 4 > 3 > 2; 1 and 2 heard |\n| 8 | 1, 2, 3, 4 all heard |\n\n";
    md << "### Operators\n\n"
          "Four operators, `op1_` .. `op4_` (`opN_` below). Only op 1 is heard by default: `op1_level` is 1 and the "
          "others are 0.\n\n";
    md << kTableHead;
    for (int f = 0; f < kNumOpFields; ++f)
    {
        const auto& spec = voiceParamSpecs()[(size_t) opParam (0, f)];
        const auto key = "opN_" + juce::String (spec.key).fromFirstOccurrenceOf ("_", false, false);
        md << row (spec, key, describeVoiceParam (opParam (0, f)), f == OpLevel ? "1 (op 1), 0 (ops 2-4)" : juce::String());
    }
    md << "\n### Sample\n\n";
    voiceRows (vp::SmpTune, vp::SmpSliceSens);
    md << "The sample itself is a child element: `<SAMPLE file=\"Samples/break.wav\"/>`. `file` is relative to the preset "
          "file; `absolute` (optional) is where to look if it isn't found there. WAV, AIFF and FLAC, mono or stereo, up to "
          "2 minutes.\n\n";
    md << "### FX\n\n";
    voiceRows (vp::Punch, vp::FltRes);
    md << "### Mix and play\n\n";
    voiceRows (vp::ChainAmt, vp::Choke);
    md << "### Stack\n\n";
    voiceRows (vp::StackCount, vp::StackInterval);

    md << "## Kit settings\n\n"
          "Attributes of `KIT`, in kit and set files. Host parameters use the same keys.\n\n";
    md << "### XY pad\n\n";
    globalRows ({ { gp::XyX, gp::XyY } });
    md << "### Chain\n\n"
          "One chain for the whole kit, fed by each sound's `chain_amt`: dynamics, then distortion, then EQ, then the "
          "output level. Stages with Follow XY on are moved by the pad around where their controls are set.\n\n";
    globalRows ({ { gp::CompAmount, gp::SafetyClip } });
    md << "### Modulators\n\n"
          "Two modulators, `mod1_` and `mod2_` (`modN_` below). Their shapes and targets are in `MOVEMENT` (below).\n\n";
    md << kTableHead;
    for (int f = 0; f < kNumModFields; ++f)
    {
        const auto g = modParam (0, f);
        const auto& spec = globalParamSpecs()[(size_t) g];
        md << row (spec, "modN_" + juce::String (spec.key).fromFirstOccurrenceOf ("_", false, false), describeGlobalParam (g));
    }
    md << "\n### Scenes\n\n";
    globalRows ({ { gp::SceneA, gp::SceneMorphOn } });

    md << "## Set-only settings\n\n"
          "Also attributes of `KIT`, but only a set keeps them; a kit file ignores them, so a kit can be swapped under a "
          "playing beat.\n\n";
    globalRows ({ { gp::MidiMode, gp::KeysVoice }, { gp::SeqPattern, gp::SeqSync } });

    md << "## MOVEMENT\n\n"
          "Inside `KIT`: the modulators' shapes and targets, and the stored scenes.\n\n"
          "```xml\n<MOVEMENT>\n  <MOD index=\"0\" points=\"0:0:0,0.5:1:0\">\n"
          "    <TARGET global=\"xy_y\" depth=\"0.25\"/>\n    <TARGET voice=\"0\" param=\"flt_cutoff\" depth=\"-0.3\"/>\n"
          "  </MOD>\n  <SCENE index=\"0\" xy_x=\"0.2\" xy_y=\"0.6\"/>\n</MOVEMENT>\n```\n\n"
          "- **MOD** `index` 0 or 1. `points`: up to " << kMaxModPoints << " points over one cycle, each `x:y` or "
          "`x:y:curve` (x and y 0..1, curve -1..1 bends the segment that starts there).\n"
          "- **TARGET**: up to " << kMaxModTargets << " per modulator. Either `global` (a kit setting that can move: the pad, "
          "the chain's numbers, `master`, `scene_morph`) or `voice` (0..7) with `param` (any number setting of a sound). "
          "`depth` -1..1 is how far it moves, in knob travel.\n"
          "- **SCENE** `index` 0..3 (A..D): the pad and chain settings, as numbers (menus by index).\n\n";

    md << "## Patterns\n\n"
          "`PATTERN` is the pattern file's element, and each `PATTERNS > PATTERN` in a set (with `index`, 0..15).\n\n"
          "| On | Attribute | Values | Default |\n|---|---|---|---|\n"
          "| PATTERN | `length` | 8..64 steps (16th notes) | 16 |\n"
          "| PATTERN | `xy` | per step, comma-separated: `-` (no change) or `x:y` (0..1 each) moves the pad | all `-` |\n"
          "| TRACK | `voice` | 0..7, the slot | required |\n"
          "| TRACK | `length` | 1..64 steps: shorter than the pattern loops on its own (polymeter) | the pattern's |\n"
          "| TRACK | `noteLength` | 0.05..1 of a step, for Gate sounds | 0.5 |\n"
          "| TRACK | `gate` | a character per step: `1` or `x` a hit, `0`, `.` or `-` a rest; spaces and `\\|` are ignored | all rests |\n"
          "| TRACK | `velocity` | per step, comma-separated, 1..127 | 100 |\n"
          "| TRACK | `pitch` | per step, -24..24 semitones | 0 |\n"
          "| TRACK | `slice` | per step, 0..31: which slice plays (Grid or Transients sounds) | 0 |\n"
          "| TRACK | `ratchet` | per step, 1..4 hits in the step | 1 |\n"
          "| TRACK | `probability` | per step, 0..100 % | 100 |\n\n"
          "Lanes shorter than the pattern are filled with the default; Batida itself writes all 64 steps. Swing and "
          "tempo are set-level settings (`seq_swing`, `seq_tempo`).\n\n";

    md << "## MIDI\n\n"
          "| Notes | What they play |\n|---|---|\n"
          "| 36-43 (C1-G1) | slots 1-8 |\n"
          "| 60-75 (C3-D#4) | patterns 1-16, while held |\n"
          "| CC 16 / CC 17 | the pad's X and Y |\n\n"
          "Note names differ between DAWs (60 is C3 in Logic and Live, C4 in Reaper, C5 in FL Studio); the numbers are "
          "the ones to go by. In Chromatic mode every channel but 10 plays `keys_voice` across the keyboard, with note 60 "
          "at the sound's own pitch.\n\n";

    md << "## Host parameters\n\n"
          "Every setting in the tables is also a host parameter: sounds as `v1_<key>` .. `v8_<key>` (named `S1 Amp "
          "Decay`...), kit and set settings by their key. Hosts see them as 0..1:\n\n"
          "- **Menus and switches:** index / (number of choices - 1).\n"
          "- **Numbers:** `(value - min) / (max - min)`, raised to a skew for settings that have one (`centre` in "
          "`batida params --json`): `skew = log(0.5) / log((centre - min) / (max - min))`, so the centre value sits at "
          "0.5.\n\n";

    md << "## The batida tool\n\n"
          "`batida` works on these files without a DAW. It's built next to the plugin and also sits inside it, at "
          "`Batida.component/Contents/Helpers/batida`. `batida help <command>` shows each command's options; `--json` "
          "gives output another program can read.\n\n"
          "| Command | Does |\n|---|---|\n"
          "| `batida render <file> -o out.wav` | renders a sound, kit or set through the whole chain, and prints what `analyze` would |\n"
          "| `batida analyze <file.wav> [--against other.wav]` | loudness, peak, length, tone, noise, pitch, width, onsets |\n"
          "| `batida validate <file>...` | everything loading lets slide |\n"
          "| `batida describe <file>` | a plain summary; a pattern as a text grid |\n"
          "| `batida new sound\\|kit\\|pattern\\|set <name>` | writes a file to start from, into the User folder |\n"
          "| `batida list` | the library, with filters |\n"
          "| `batida params` | the tables on this page, for the installed version |\n";
    return md;
}

} // namespace batida
