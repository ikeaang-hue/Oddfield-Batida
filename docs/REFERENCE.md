# Batida files and settings

Everything Batida saves is a plain XML file: a **sound**, a **kit**, a **pattern** or a **set**. This page lists every element and setting in them, so a file can be written or edited by hand or by a script. It's generated from Batida's own parameter tables. `batida params` prints the same data for the version that's installed, and `batida validate` checks a file against it.

## Files

| Level | File | Holds | Folder in the library |
|---|---|---|---|
| Sound | `.batida-sound` | one slot's sound (`VOICE`) | `User/Sounds/<Category>/` |
| Kit | `.batida-kit` | 8 sounds, the kit chain, the pad, modulators and scenes (`KIT`) | `User/Kits/` |
| Pattern | `.batida-pattern` | one pattern (`PATTERN`) | `User/Patterns/` |
| Set | `.batida-set` | everything: the kit, all 16 patterns, the sequencer, MIDI and master (`KIT`, `PATTERNS`) | `User/Sets/` |

The library is `~/Music/Oddfield/Batida/`. A file saved into its `User` folder shows up in LIB and the ◀ name ▶ strips within a few seconds while a Batida window is open. When the file behind the loaded sound, kit or pattern changes, its strip shows a lime ↻: click it to load the new version (one undo step).

## The smallest files

Only what differs from the defaults needs to be written.

**A sound**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<BATIDA type="sound">
  <INFO name="Deep Kick" category="kick" tags="warm"/>
  <VOICE src_mode="FM" fm_pitch="-24" amp_decay="450" pitch_amt="30" pitch_decay="45"
         punch="0.4" drive="0.2" drive_type="soft"/>
</BATIDA>
```

**A kit**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<BATIDA type="kit">
  <INFO name="Two Hits" tags="dark"/>
  <KIT xy_x="0.3" xy_y="0.5">
    <VOICE index="0" name="Kick" fm_pitch="-24" amp_decay="400" pitch_amt="28" pitch_decay="40"/>
    <VOICE index="2" name="Snare" op1_wave="1" amp_decay="180" flt_type="band-pass"
           flt_cutoff="2500" flt_res="0.3"/>
  </KIT>
</BATIDA>
```

**A pattern**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<BATIDA type="pattern">
  <INFO name="Four Floor"/>
  <PATTERN length="16">
    <TRACK voice="0" gate="x... x... x... x..."/>
    <TRACK voice="2" gate=".... x... .... x..." velocity="100,100,100,100,120"/>
    <TRACK voice="6" gate="..x. ..x. ..x. ..x." probability="100,100,80"/>
  </PATTERN>
</BATIDA>
```

**A set**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<BATIDA type="set">
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
```

## How values are read

- **Units:** numbers are in the unit the tables give (ms, Hz, dB, semitones...). Amounts without a unit are fractions: 0.5 is what the plugin shows as 50%.
- **Out of range:** a number outside its range is clamped.
- **Menus:** by name, in any case (`drive_type="soft"`), or by index from 0.
- **Switches:** 1 or 0, true or false, on or off, yes or no.
- **Missing settings** take their defaults. A `VOICE` starts from the default sound (the Default column), not from the slot's sound in the Neutral kit; a kit's slots with no `VOICE` keep the Neutral kit's sounds.
- **Slots and patterns count from 0** in files: `VOICE index="0"` and `TRACK voice="0"` are slot 1, `PATTERN index="0"` is pattern 1.
- **Unknown elements and settings** are skipped. `batida validate` lists them, with the nearest known name.
- `<BATIDA type="...">` must match the file's extension. `format` (default 1) and `madeWith` are optional; a file from a newer format still opens, and what this version knows is read.

## INFO

`<INFO name="..." author="..." category="..." tags="..."/>`, all optional. Without a name, the file's name is used. **category** (sounds): one of kick, snare, hat, perc, fx, bass, texture. **tags**: comma-separated words; the browser offers warm, harsh, digital, metallic, organic, and any other word works as a tag too.

## Sound settings

Attributes of `VOICE`: the whole element in a sound file, and each `KIT > VOICE` in kits and sets (which also take `index`, 0..7, and `name`). The same settings are the host parameters `v1_<key>` .. `v8_<key>`.

### Source and play

| Setting | Range | Default | What it does |
|---|---|---|---|
| `src_mode` | FM, Sample, Layer | FM | What makes the sound: FM (four operators), Sample, or Layer (both, mixed by balance). |
| `balance` | 0..1 | 0.5 | Layer only: 0 = only the sample, 0.5 = both at full level, 1 = only FM. |
| `level` | -60..6 dB | 0 | The sound's level. |
| `pan` | -1..1 | 0 | -1 left, 0 centre, 1 right. |
| `vel_sens` | 0..1 | 0.7 | How much velocity changes the level: 0 = not at all, 1 = fully. |
| `play_mode` | One-shot, Gate | One-shot | One-shot plays the whole envelope from a hit; Gate holds while the note (or the step's noteLength) lasts. |
| `glide` | 0..2000 ms | 0 | Time to slide from the previous note's pitch (bass lines and slides). |

### Envelopes

| Setting | Range | Default | What it does |
|---|---|---|---|
| `amp_attack` | 0..2000 ms | 0 | Amp envelope attack. |
| `amp_decay` | 5..8000 ms | 400 | Amp envelope decay: for a one-shot, how long the hit rings. |
| `amp_sustain` | 0..1 | 0 | Amp envelope sustain level (Gate mode holds here). |
| `amp_release` | 5..8000 ms | 80 | Amp envelope release, after the note ends. |
| `pitch_amt` | -48..48 semitones | 0 | Pitch envelope depth: the hit starts this far above (or below) its pitch and falls back. +20..+40 makes a kick's drop. |
| `pitch_decay` | 2..2000 ms | 60 | How fast the pitch envelope falls back. |

### FM

| Setting | Range | Default | What it does |
|---|---|---|---|
| `fm_pitch` | -48..48 semitones | 0 | Transposes the FM source. With op ratio 1 the note 60 (C3) plays at 261.6 Hz; -24 puts it at 65.4 Hz (a kick or bass). |
| `fm_algo` | 1, 2, 3, 4, 5, 6, 7, 8 | 1 | Operator routing (see Algorithms). Op 4 carries the feedback in every algorithm. |
| `fm_feedback` | 0..1 | 0 | Op 4 feeding back into itself: from brighter to noisy. |
| `fm_harm` | -1..1 | 0 | Macro: 0 leaves the modulators' ratios as set; towards -1 more harmonic, towards 1 more inharmonic (bells, metal). |
| `fm_bright` | -1..1 | 0 | Macro: scales every modulator's depth (x0.5 at -1, x2 at 1). Brighter or darker. |
| `fm_bdecay` | -1..1 | 0 | Macro: scales the modulators' decay and release (x4 longer at -1, x4 shorter at 1): how fast the brightness fades. |
| `fm_grit` | -1..1 | 0 | Macro: added to fm_feedback. |

**Algorithms** (`fm_algo`; `a > b` means a modulates b, and the operators on the right are heard):

| # | Routing |
|---|---|
| 1 | 4 > 3 > 2 > 1 |
| 2 | (3 + 4) > 2 > 1 |
| 3 | (4 > 3) + 2 > 1 |
| 4 | 4 > 3, 2 > 1 (1 and 3 heard) |
| 5 | 4 > (1, 2, 3) |
| 6 | 4 > 3; 1, 2, 3 heard |
| 7 | 4 > 3 > 2; 1 and 2 heard |
| 8 | 1, 2, 3, 4 all heard |

### Operators

Four operators, `op1_` .. `op4_` (`opN_` below). Only op 1 is heard by default: `op1_level` is 1 and the others are 0.

| Setting | Range | Default | What it does |
|---|---|---|---|
| `opN_ratio` | 0.125..32 x the note | 1 | Frequency as a multiple of the note (1 = the note, 2 = an octave up). Ignored when Fixed is on. |
| `opN_fixed` | 0 or 1 | 0 | On: the operator ignores the note and runs at its Freq. |
| `opN_freq` | 10..20000 Hz | 1000 | The fixed frequency, when Fixed is on. |
| `opN_level` | 0..1 | 1 (op 1), 0 (ops 2-4) | A carrier's output level, or a modulator's modulation depth (which carriers and modulators are depends on fm_algo). |
| `opN_wave` | 0..1 | 0 | Waveform morph: 0 sine, then triangle, saw and square; 1 is white noise, which ignores pitch (hats, snares, claps). |
| `opN_fold` | 0..1 | 0 | Wavefolding: bends the waveform back on itself for brighter, harder overtones. |
| `opN_attack` | 0..2000 ms | 0 | The operator's envelope attack. |
| `opN_decay` | 5..8000 ms | 600 | The operator's envelope decay. |
| `opN_sustain` | 0..1 | 1 | The operator's envelope sustain level. |
| `opN_release` | 5..8000 ms | 1500 | The operator's envelope release. |

### Sample

| Setting | Range | Default | What it does |
|---|---|---|---|
| `smp_tune` | -24..24 semitones | 0 | Sample pitch. It changes speed (no time-stretching). |
| `smp_start` | 0..1 | 0 | Where the sample starts, as a fraction of its length. |
| `smp_end` | 0..1 | 1 | Where the sample ends, as a fraction of its length. |
| `smp_reverse` | 0 or 1 | 0 | Plays the sample backwards. |
| `smp_fade_in` | 0..1000 ms | 0 | Fade in from the start point. |
| `smp_fade_out` | 0..2000 ms | 0 | Fade out before the end point. |
| `smp_gain` | -24..24 dB | 0 | The sample's gain before the sound's FX. |
| `smp_loop` | 0 or 1 | 0 | Gate mode: loops between the loop points while the note is held. |
| `smp_loop_start` | 0..1 | 0 | Loop start, as a fraction of the sample's length. |
| `smp_loop_end` | 0..1 | 1 | Loop end, as a fraction of the sample's length. |
| `smp_slice_mode` | Off, Grid, Transients | Off | Off, Grid (even slices) or Transients (slices start at the hits). A pattern step's slice lane picks which slice plays. |
| `smp_slices` | 2..32 slices | 8 | How many slices (Grid), or the most (Transients). |
| `smp_slice_sens` | 0..1 | 0.5 | Transients: how weak a hit can be and still start a slice. |

The sample itself is a child element: `<SAMPLE file="Samples/break.wav"/>`. `file` is relative to the preset file; `absolute` (optional) is where to look if it isn't found there. WAV, AIFF and FLAC, mono or stereo, up to 2 minutes.

### FX

| Setting | Range | Default | What it does |
|---|---|---|---|
| `punch` | 0..1 | 0 | Transient and compression in one control: more snap and body. |
| `drive` | 0..1 | 0 | Distortion amount (level compensated). |
| `drive_type` | Soft, Hard, Fold, Crush | Soft | Soft (warm), Hard (clipped), Fold (wavefolded) or Crush (bit and rate reduced). |
| `flt_type` | Low-pass, High-pass, Band-pass | Low-pass | The sound's filter: Low-pass, High-pass or Band-pass. |
| `flt_cutoff` | 20..20000 Hz | 20000 | Filter cutoff. 20000 on the low-pass is open. |
| `flt_res` | 0..1 | 0 | Filter resonance. |

### Mix and play

| Setting | Range | Default | What it does |
|---|---|---|---|
| `chain_amt` | 0..1 | 1 | How much of the sound goes through the kit chain: 0 = dry (bypasses it), 1 = all through it. |
| `mute` | 0 or 1 | 0 | Mutes the sound. |
| `solo` | 0 or 1 | 0 | Solos the sound. |
| `choke` | Off, 1, 2, 3, 4 | Off | Off, or a group 1-4: a hit cuts the other sounding sounds in its group (open and closed hats). |

### Stack

| Setting | Range | Default | What it does |
|---|---|---|---|
| `stack` | 1, 2, 3, 4 | 1 | How many copies of the source play (thickness, or a chord with stack_interval). |
| `stack_detune` | 0..100 cents | 12 | The copies spread evenly across plus and minus this many cents. |
| `stack_spread` | 0..1 | 0.6 | The copies spread across the stereo field: 0 = all centred, 1 = full width. |
| `stack_interval` | Unison, Octave, Fifth, Minor, Minor 7, Major | Unison | The copies' notes, in order: Unison, Octave (0 12 0 12), Fifth, Minor (0 3 7 12), Minor 7 (0 3 7 10), Major. |

## Kit settings

Attributes of `KIT`, in kit and set files. Host parameters use the same keys.

### XY pad

| Setting | Range | Default | What it does |
|---|---|---|---|
| `xy_x` | 0..1 | 0.5 | The pad's X, character: warm (0) through aggressive to digital (1). Moves the stages that follow XY. |
| `xy_y` | 0..1 | 0.2 | The pad's Y, heat: clean (0) to destroyed (1). Crushes more than it squashes, and stays about as loud. |

### Chain

One chain for the whole kit, fed by each sound's `chain_amt`: dynamics, then distortion, then EQ, then the output level. Stages with Follow XY on are moved by the pad around where their controls are set.

| Setting | Range | Default | What it does |
|---|---|---|---|
| `comp_amount` | 0..1 | 0 | Kit compressor: how much. Make-up gain is automatic, so this changes density, not level. |
| `comp_attack` | 0.1..100 ms | 10 | Kit compressor attack. |
| `comp_release` | 10..1000 ms | 120 | Kit compressor release. |
| `comp_mix` | 0..1 | 1 | Kit compressor dry/wet (parallel compression). |
| `comp_detector` | Internal, Sidechain | Internal | What the compressor listens to: its own input, or the plugin's sidechain input from the DAW. |
| `comp_follow` | 0 or 1 | 1 | The compressor follows the XY pad. |
| `dist_drive` | 0..1 | 0 | Kit distortion amount (level compensated). |
| `dist_type` | 0..1 | 0.5 | Distortion character, blended: 0 warm (tape, tube), 0.5 aggressive (clip, fold), 1 digital (crush, decimate). |
| `dist_low_keep` | 40..300 Hz | 90 | Below this frequency the signal skips the distortion, so the sub stays clean. |
| `exc_amount` | 0..1 | 0 | Exciter: adds high harmonics. |
| `exc_tone` | 2000..12000 Hz | 5000 | Where the exciter starts. |
| `dist_follow` | 0 or 1 | 1 | The distortion and exciter follow the XY pad. |
| `eq_hp` | 20..1000 Hz | 20 | Kit high-pass. |
| `eq_lp` | 1000..20000 Hz | 20000 | Kit low-pass (20000 is open). |
| `eq_low` | -12..12 dB | 0 | Low shelf. |
| `eq_mid` | -12..12 dB | 0 | Mid band. |
| `eq_high` | -12..12 dB | 0 | High shelf. |
| `eq_follow` | 0 or 1 | 1 | The EQ follows the XY pad. |
| `chain_out` | -24..12 dB | 0 | The kit chain's output level. |
| `safety_clip` | 0 or 1 | 1 | A soft ceiling just under 0 dBFS at the very end. |

### Modulators

Two modulators, `mod1_` and `mod2_` (`modN_` below). Their shapes and targets are in `MOVEMENT` (below).

| Setting | Range | Default | What it does |
|---|---|---|---|
| `modN_mode` | Sync, Free, One-shot | Sync | Sync loops the shape in time with the tempo; Free runs at modN_hz; One-shot plays it once per trigger. |
| `modN_rate` | 1/32, 1/16T, 1/16, 1/16D, 1/8T, 1/8, 1/8D, 1/4T, 1/4, 1/4D, 1/2, 1 bar, 2 bars, 4 bars, 8 bars | 1 bar | Sync and One-shot: how long one pass of the shape takes. |
| `modN_hz` | 0.01..20 Hz | 1 | Free: cycles per second. |
| `modN_polarity` | Unipolar, Bipolar | Unipolar | Unipolar moves targets one way from where they're set; Bipolar moves them both ways. |
| `modN_smooth` | 0..1 | 0 | Rounds the shape's corners. |
| `modN_humanise` | 0..1 | 0 | Random variation per cycle. |
| `modN_amount` | 0..1 | 1 | Scales every target's depth. |
| `modN_trigger` | Any hit, Pattern start, Sound 1, Sound 2, Sound 3, Sound 4, Sound 5, Sound 6, Sound 7, Sound 8 | Any hit | What restarts the shape: any hit, the pattern's start, or one sound's hits. |

### Scenes

| Setting | Range | Default | What it does |
|---|---|---|---|
| `scene_a` | A, B, C, D | A | The scene the morph starts from. |
| `scene_b` | A, B, C, D | B | The scene the morph goes to. |
| `scene_morph` | 0..1 | 0 | 0 = scene_a, 1 = scene_b, in between blended. |
| `scene_morph_on` | 0 or 1 | 0 | The morph drives the chain (off: the chain stays where its controls are). |

## Set-only settings

Also attributes of `KIT`, but only a set keeps them; a kit file ignores them, so a kit can be swapped under a playing beat.

| Setting | Range | Default | What it does |
|---|---|---|---|
| `midi_mode` | Drum map, Chromatic | Drum map | Drum map: notes 36-43 play slots 1-8 on any channel. Chromatic: every channel but 10 plays keys_voice across the keyboard. |
| `master` | -60..6 dB | -8 | Batida's output level (headroom by default, since hits add up). |
| `keys_voice` | 1, 2, 3, 4, 5, 6, 7, 8 | 1 | The sound that Chromatic mode plays. |
| `seq_pattern` | 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 | 1 | The pattern that plays (and the one the SEQ page shows). |
| `seq_run` | Keys, Transport | Keys | Keys: patterns play while their key (60-75) is held. Transport: the pattern runs with the host's play (or seq_play). |
| `seq_play` | 0 or 1 | 0 | Transport mode: plays with the host stopped. |
| `seq_tempo` | 40..240 bpm | 120 | The tempo when not following the host (seq_sync off, or the host stopped). |
| `seq_swing` | 0.5..0.75 | 0.5 | 0.5 straight, up to 0.75 (heavy shuffle). Delays every second 16th. |
| `seq_quantise` | Step, Beat, Bar | Beat | When a pattern key's pattern starts: the next step, beat or bar. |
| `seq_latch` | 0 or 1 | 0 | A pattern key keeps its pattern playing after release. |
| `seq_sync` | 0 or 1 | 1 | Follow the host's tempo and position. |

## MOVEMENT

Inside `KIT`: the modulators' shapes and targets, and the stored scenes.

```xml
<MOVEMENT>
  <MOD index="0" points="0:0:0,0.5:1:0">
    <TARGET global="xy_y" depth="0.25"/>
    <TARGET voice="0" param="flt_cutoff" depth="-0.3"/>
  </MOD>
  <SCENE index="0" xy_x="0.2" xy_y="0.6"/>
</MOVEMENT>
```

- **MOD** `index` 0 or 1. `points`: up to 32 points over one cycle, each `x:y` or `x:y:curve` (x and y 0..1, curve -1..1 bends the segment that starts there).
- **TARGET**: up to 8 per modulator. Either `global` (a kit setting that can move: the pad, the chain's numbers, `master`, `scene_morph`) or `voice` (0..7) with `param` (any number setting of a sound). `depth` -1..1 is how far it moves, in knob travel.
- **SCENE** `index` 0..3 (A..D): the pad and chain settings, as numbers (menus by index).

## Patterns

`PATTERN` is the pattern file's element, and each `PATTERNS > PATTERN` in a set (with `index`, 0..15).

| On | Attribute | Values | Default |
|---|---|---|---|
| PATTERN | `length` | 8..64 steps (16th notes) | 16 |
| PATTERN | `xy` | per step, comma-separated: `-` (no change) or `x:y` (0..1 each) moves the pad | all `-` |
| TRACK | `voice` | 0..7, the slot | required |
| TRACK | `length` | 1..64 steps: shorter than the pattern loops on its own (polymeter) | the pattern's |
| TRACK | `noteLength` | 0.05..1 of a step, for Gate sounds | 0.5 |
| TRACK | `gate` | a character per step: `1` or `x` a hit, `0`, `.` or `-` a rest; spaces and `\|` are ignored | all rests |
| TRACK | `velocity` | per step, comma-separated, 1..127 | 100 |
| TRACK | `pitch` | per step, -24..24 semitones | 0 |
| TRACK | `slice` | per step, 0..31: which slice plays (Grid or Transients sounds) | 0 |
| TRACK | `ratchet` | per step, 1..4 hits in the step | 1 |
| TRACK | `probability` | per step, 0..100 % | 100 |

Lanes shorter than the pattern are filled with the default; Batida itself writes all 64 steps. Swing and tempo are set-level settings (`seq_swing`, `seq_tempo`).

## MIDI

| Notes | What they play |
|---|---|
| 36-43 (C1-G1) | slots 1-8 |
| 60-75 (C3-D#4) | patterns 1-16, while held |
| CC 16 / CC 17 | the pad's X and Y |

Note names differ between DAWs (60 is C3 in Logic and Live, C4 in Reaper, C5 in FL Studio); the numbers are the ones to go by. In Chromatic mode every channel but 10 plays `keys_voice` across the keyboard, with note 60 at the sound's own pitch.

## Host parameters

Every setting in the tables is also a host parameter: sounds as `v1_<key>` .. `v8_<key>` (named `S1 Amp Decay`...), kit and set settings by their key. Hosts see them as 0..1:

- **Menus and switches:** index / (number of choices - 1).
- **Numbers:** `(value - min) / (max - min)`, raised to a skew for settings that have one (`centre` in `batida params --json`): `skew = log(0.5) / log((centre - min) / (max - min))`, so the centre value sits at 0.5.

## The batida tool

`batida` works on these files without a DAW. It's built next to the plugin and also sits inside it, at `Batida.component/Contents/Helpers/batida`. `batida help <command>` shows each command's options; `--json` gives output another program can read.

| Command | Does |
|---|---|
| `batida render <file> -o out.wav` | renders a sound, kit or set through the whole chain, and prints what `analyze` would |
| `batida analyze <file.wav> [--against other.wav]` | loudness, peak, length, tone, noise, pitch, width, onsets |
| `batida validate <file>...` | everything loading lets slide |
| `batida describe <file>` | a plain summary; a pattern as a text grid |
| `batida new sound\|kit\|pattern\|set <name>` | writes a file to start from, into the User folder |
| `batida list` | the library, with filters |
| `batida params` | the tables on this page, for the installed version |
