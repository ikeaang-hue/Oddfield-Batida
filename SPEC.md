# Negative Space Batida: Design Spec

*Version 1.1, agreed 2026-09-24 (1.1: MIDI mode, macro behaviour, test host)*

## 1. Identity

- **Publisher:** Negative Space (manufacturer code `Ngsp`)
- **Plugin:** Batida (plugin code `Btda`). Portuguese for "beat" and "hit".
- **What it is:** a sound-design drum instrument. It works for general use, but is strongest on
  synthetic and heavy genres such as techno, breakbeat and glitch.
- **Core idea:** sound design first; the sequencer serves the sound.
- **Two roles:**
  - **Scratch builder:** deep editing of every sound.
  - **Inspirer:** the XY pad and guided variations.
- **Lean rule:** every feature should do at least two jobs. Voices stay simple; the deep
  processing lives once, at kit level.

## 2. Signal flow

```
8 voices:  Source (Sample | FM | Layer) → [Body: deferred] → Punch → Drive → Filter → [Clean toggle]
                                   ↓
Kit:       Dynamics → Distortion (+ exciter, clean low end) → Filter/EQ → Level → out
                          ↑
                   XY pad (X = character, Y = heat)      2 drawn modulators → any parameter
```

## 3. Voices (×8)

The eight voices are identical, general-purpose slots. Any slot can hold any sound.

### Source (one of three)

**Sample**
- Controls: start, end, tune (key-tracked), reverse, fade in/out, gain, loop points.
- Modes: **one-shot** (plays to the end) or **gate** (plays while the note is held, and loops
  if loop points are set).
- **Slice mode:** cuts a loop into slices, either by transients or on an even grid. Each
  sequencer step chooses which slice plays.
- Formats: WAV, AIFF, FLAC; mono or stereo.
- No time-stretching. Changing pitch changes speed.

**FM**
- **4 operators** and about **8 algorithms**, shown as diagrams.
- Per operator: ratio or fixed frequency, level, its own envelope, and a morphable waveform
  (sine → triangle → saw → square, plus fold).
- Feedback.
- **Macro surface** on top of the operators:
  - *Harmonic ↔ Inharmonic:* chooses from curated ratio sets.
  - *Brightness:* overall modulation depth.
  - *Brightness decay:* how fast the brightness fades.
  - *Grit:* feedback.

  Each macro is an automatable host parameter, applied as an offset on top of the operator
  settings. Each operator knob shows a second marker at its effective value, so the user sees
  the macros move the real parameters and learns FM by watching.

**Layer**
- Sample and FM together, with a balance control (e.g. a sampled transient over an FM body).

### Voice chain
- **Punch:** one transient/compression control.
- **Drive:** amount, plus type (soft, hard, fold, crush).
- **Filter:** low-pass, high-pass or band-pass, with cutoff and resonance.
- **Clean toggle:** voices go through the kit chain by default; this toggle bypasses it for
  that voice.

### Envelopes and play
- **Envelopes:** amp and pitch. FM operators have their own envelopes as well.
- **Monophonic:** one note at a time per voice; a new note retriggers.
- **Glide:** one knob per voice, for bass lines and slides.
- **Range:** voices are chromatic and can sustain. The sound leans percussive, but basses,
  stabs and textures are in scope.

### Body / resonator (deferred)
- The architecture keeps a slot between source and chain.
- It gets added once prototype CPU measurements show there's room.

## 4. Kit chain

1. **Dynamics:** compressor with amount, attack, release and **mix** (for parallel compression).
2. **Distortion / saturation:**
   - Character types from warm (tape/tube), through aggressive (clip/fold), to digital
     (crush/decimate).
   - **Clean low end:** a crossover that keeps the sub intact.
   - **Exciter:** a high-band control inside this stage, sharing the distortion engine.
3. **Filter/EQ:** high-pass and low-pass, plus broad low/mid/high bands.
4. **Output level.**

Reverb, delay and stereo/spatial effects are left to the DAW.

## 5. XY pad

- Acts on the **whole kit's** distortion character.
- **X = character:** warm → aggressive → digital.
- **Y = heat:** clean → destroyed.
- The exciter and the clean-low-end crossover follow along, so extreme settings stay usable.
- Positions can be stored per sequencer step (see §8).

## 6. Modulators (×2, kit level)

- **Shape:** drawn with points and curves.
- **Modes:** tempo-synced loop, fire once per trigger, or free-running.
- **Routing:** drag onto any parameter, with a depth per target.
- **Smooth/humanize:** an amount per modulator.

## 7. Guided variations (per voice)

- **Vary** generates about 4 alternatives close to the current sound. An **amount** control sets
  how far they stray.
- **Directions:** brighter/darker, shorter/longer, tonal/noisy, cleaner/dirtier.
- **Locks** by section: source, chain, envelopes.
- **Automatic dud filtering:** silent, clipping and near-duplicate results never reach the user.
- **History:** full undo/redo, and one click saves a result to the library.
- *Breeding* (combining two sounds) is postponed to a later version.

## 8. Sequencer

- **16 patterns.**
- **Pattern length:** 16 steps by default, resizable from **8 to 64**.
- **Per-track length (polymeter):** from 1 up to the pattern length.
- **Per-step lanes:** gate, velocity, pitch, slice, ratchet (1–4), probability.
- **Pattern-wide XY lane:** each step can move the character pad.
- **Swing.**
- **Host sync:** follows the DAW's tempo and position. There's an internal play button for when
  the DAW is stopped. Pattern selection is automatable.
- **Excluded:** song mode and arrangement (the DAW does these).

## 9. MIDI

A **MIDI mode** switch chooses between:
- **Drum map, any channel** (default): GM notes 36–43 trigger voices 1–8 on every channel.
- **Split:** channel 10 uses the drum map; channels 1–8 play voices 1–8 chromatically.

## 10. Library and presets

**File levels**
- **Sound:** one voice.
- **Kit:** 8 voices, the kit chain, XY position and modulators.
- **Pattern:** reusable sequences.

**Samples**
- Presets reference samples by path.
- **Collect samples** copies them next to the kit so it's portable.
- Missing samples trigger a **search/relink** prompt.

**Library folder**
- On disk, with *Factory* and *User* areas.
- Files are in a readable format, so the user can manage them in Finder.

**Browser**
- Categories: kick, snare, hat, perc, FX, bass, texture.
- Character tags: warm, harsh, digital, metallic, organic.
- Search and favourites.
- Click to preview, and next/previous while a pattern plays.

**Getting samples in**
- Drag from Finder onto a voice.
- Browse folders with preview.

## 11. Factory content (preset factory)

- **Target:** about 100–150 sounds, 10–15 kits and about 10 patterns.
- **Method:** an offline tool built on the same engine.
  1. Archetype recipes per category.
  2. Guided variations around each archetype.
  3. Render, measure and auto-tag each sound, discarding duds.
  4. Assemble kits from sounds with matching character.
  5. The user listens and keeps or rejects each result on a review page.
- **Samples:** rendered from the engine itself, or CC0.

## 12. UI (for now)

- A minimal demo UI: stock controls, no custom graphics.
- **Play view:** XY pad, macros, pattern grid.
- **Edit view:** the selected voice only.
- UI code is kept separate from the engine, so a proper visual design can replace it later.

## 13. Technical

- **Stack:** C++ with JUCE 9 (submodule in `external/JUCE`), built with CMake and the Xcode
  generator.
- **Platform:** macOS only. Windows is not a consideration for now.
- **Formats:**
  - **AU:** the primary format. Everything is built and tested as AU first.
  - **VST3 (macOS):** added once everything else is done.
  - No standalone app.
- **Test host:** Logic Pro.

## 14. Build order

Each phase ends with something loadable and audible as an AU.

1. **Voice engine:** FM and sample sources, voice chain, envelopes, glide, MIDI, minimal UI.
2. **Sequencer:**
   - patterns and 8–64 steps;
   - velocity, pitch, slice, ratchet and probability lanes;
   - swing;
   - host sync.
3. **Kit chain and XY pad:** dynamics, distortion with exciter, filter/EQ, Clean toggles,
   per-step XY.
4. **Movement and inspiration:** the two drawn modulators and guided variations.
5. **Library:** sound/kit/pattern files, the browser, tags, sample relinking and collecting.
6. **Preset factory:** factory content and the user's listening pass.
7. **VST3 build** and cross-host checks on macOS.
8. **Later:**
   - resonator body (if CPU allows);
   - a proper visual design;
   - breeding.

## 15. Out of scope (v1)

- Reverb, delay and spatial effects.
- Time-stretching.
- Song and arrangement mode.
- Wavetable synthesis.
- Sound breeding.
- Polyphony.
- The resonator body (until CPU measurements allow).
- A polished visual design.
- A standalone app.
- Windows.
