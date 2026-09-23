# Negative Space Batida: Design Spec

*Version 1.4, agreed 2026-09-24 (1.1: MIDI mode, macro behaviour, test host; 1.2: Chromatic mode replaces Split; 1.3: noise waveform, 2× FM; 1.4: the chain as the core, XY operation, per-voice Chain amount, sidechain input, chain before sequencer)*

## 1. Identity

- **Publisher:** Negative Space (manufacturer code `Ngsp`)
- **Plugin:** Batida (plugin code `Btda`). Portuguese for "beat" and "hit".
- **What it is:** a sound-design drum instrument. It works for general use, but is strongest on
  synthetic and heavy genres such as techno, breakbeat and glitch.
- **Core idea:** sound design first; the sequencer serves the sound.
- **The spirit is the chain:** the kit chain, and how it is operated, is what makes Batida
  Batida. It is played as a whole from the XY pad, with precise control underneath (§4, §5).
- **Two roles:**
  - **Scratch builder:** deep editing of every sound.
  - **Inspirer:** the XY pad and guided variations.
- **Lean rule:** every feature should do at least two jobs. Voices stay simple; the deep
  processing lives once, at kit level.

## 2. Signal flow

```
voice → [Body: deferred] → punch → drive → filter → level/pan ─┬─ dry (1 − Chain amount) ───────┐
                                                               └─ wet (Chain amount) ─┐          │
                                                                                      ↓          │
Kit chain (wet sum of all voices):                                                               │
   Dynamics → Distortion (+ exciter, clean low end) → Filter/EQ → Output ────────────── + ←──────┘ → master
      ↑ sidechain input (optional)       ↑
                             XY pad (X = character, Y = heat), per-step XY, 2 modulators
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
  (sine → triangle → saw → square → noise, plus fold). Noise is white and ignores pitch; it is
  the clean noise source for hats, snares and claps.
- The FM source runs at 2× oversampling, so overtones above the audible range don't fold back
  as inharmonic fizz.
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
- **Chain amount** (replaces the Clean toggle): a per-voice dry/wet crossfade into the kit
  chain. 100% (default) = fully through the chain; 0% = dry, bypassing it; in between, both are
  heard. Taken after the voice's level and pan. Automatable, so a voice can move in and out.

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

One shared chain, fed by the wet part of every voice (see Chain amount, §3). The dry parts are
added back after it. The order is fixed.

1. **Dynamics:** compressor with amount, attack, release and **mix** (for parallel compression).
   - **Sidechain:** the detector listens either to the chain's own signal (default) or to the
     plugin's **sidechain input** from the DAW (in Logic, the Side Chain menu). Ducking and
     pumping are done in the DAW; there is no internal voice-keyed ducking.
2. **Distortion / saturation:**
   - Character types from warm (tape/tube), through aggressive (clip/fold), to digital
     (crush/decimate), blended continuously.
   - **Clean low end:** a crossover that keeps the sub intact.
   - **Exciter:** a high-band control inside this stage, sharing the distortion engine.
3. **Filter/EQ:** high-pass and low-pass, plus broad low/mid/high bands.
4. **Output level.**

**Precise control:** every stage has its own knobs on the Chain page (compressor amount,
attack, release, mix, sidechain source; distortion drive, type blend, exciter amount and tone,
low-end crossover; HP, LP and low/mid/high EQ; output level). Each stage has a **Follow XY**
toggle (on by default); off, the stage stays exactly where its knobs are.

Reverb, delay and stereo/spatial effects are left to the DAW.

## 5. XY pad

The main way the chain is operated: one gesture moves the whole chain.

- **Y = heat** (clean → destroyed): raises drive, compression and exciter together.
  **Automatic gain compensation** keeps loudness roughly steady, so heat changes character, not
  volume. The clean-low-end crossover rises with heat, so the sub stays solid.
- **X = character** (warm → aggressive → digital): blends the distortion types continuously;
  the exciter's tone follows.
- **Offsets, like the FM macros:** the stage knobs set a base; the XY pushes the stages around
  it. It never overwrites the knobs, and each affected knob shows an orange marker at its
  effective value. X and Y are automatable host parameters.
- Positions can be stored per sequencer step (see §8) and moved by the modulators (§6).
- **Chain scenes** (storing and morphing whole chain states) are deferred to phase 4, with the
  modulators and variations.

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
- **Chromatic:** every channel except 10 plays one chosen voice, the **Keys Voice**, like a mono
  synth. Channel 10 still uses the drum map. Keys Voice is an automatable parameter that also
  follows the voice selected in the editor.

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
2. **Kit chain and XY pad** (moved ahead of the sequencer, since the chain is the core):
   dynamics with sidechain input, distortion with exciter and clean low end, filter/EQ, output;
   XY pad with gain compensation and effective-value markers; Follow XY per stage; per-voice
   Chain amount.
3. **Sequencer**, built to play the chain as well as the voices:
   - patterns and 8–64 steps;
   - velocity, pitch, slice, ratchet and probability lanes;
   - the per-step XY lane;
   - swing;
   - host sync.
4. **Movement and inspiration:** the two drawn modulators, guided variations, and chain scenes.
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
