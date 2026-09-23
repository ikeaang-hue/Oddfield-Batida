# Negative Space Batida: Design Spec

*Version 1.8, agreed 2026-09-24 (1.1: MIDI mode, macro behaviour, test host; 1.2: Chromatic mode replaces Split; 1.3: noise waveform, 2× FM; 1.4: the chain as the core, XY operation, per-sound Chain amount, sidechain input, chain before sequencer; 1.5: details settled while building phase 2; 1.6: pattern keys; 1.7: library details and the Set level; 1.8: visual design (direction C, "Signal"), "sound" replaces "voice")*

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
- **Lean rule:** every feature should do at least two jobs. Sounds stay simple; the deep
  processing lives once, at kit level.

## 2. Signal flow

```
sound → [Body: deferred] → punch → drive → filter → level/pan ─┬─ dry (1 − Chain amount) ───────┐
                                                               └─ wet (Chain amount) ─┐          │
                                                                                      ↓          │
Kit chain (wet sum of all sounds):                                                               │
   Dynamics → Distortion (+ exciter, clean low end) → Filter/EQ → Output ────────────── + ←──────┘ → master
      ↑ sidechain input (optional)       ↑
                             XY pad (X = character, Y = heat), per-step XY, 2 modulators
```

## 3. Sounds (×8)

The eight sound slots are identical and general-purpose. Any slot can hold any sound.

*Naming:* everything the user sees says **sound** (or **slot**, where the position matters), never
"voice", which suggests human vocals. The code and parameter IDs keep "voice" internally, so saved
projects keep loading (§12).

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
  settings. Each operator control shows a lime marker at its effective value, so the user sees
  the macros move the real parameters and learns FM by watching.

**Layer**
- Sample and FM together, with a balance control (e.g. a sampled transient over an FM body).

### FX (per sound)
- **Punch:** one transient/compression control.
- **Drive:** amount, plus type (soft, hard, fold, crush).
- **Filter:** low-pass, high-pass or band-pass, with cutoff and resonance.
- **Chain amount** (replaces the Clean toggle): a per-sound dry/wet crossfade into the kit
  chain. 100% (default) = fully through the chain; 0% = dry, bypassing it; in between, both are
  heard. Taken after the sound's level and pan. Automatable, so a sound can move in and out.

### Envelopes and play
- **Envelopes:** amp and pitch. FM operators have their own envelopes as well.
- **Monophonic:** one note at a time per sound; a new note retriggers.
- **Glide:** one control per sound, for bass lines and slides.
- **Range:** sounds are chromatic and can sustain. They lean percussive, but basses,
  stabs and textures are in scope.

### Body / resonator (deferred)
- The architecture keeps a slot between source and chain.
- It gets added once prototype CPU measurements show there's room.

## 4. Kit chain

One shared chain, fed by the wet part of every sound (see Chain amount, §3). The dry parts are
added back after it. The order is fixed.

1. **Dynamics:** compressor with amount, attack, release and **mix** (for parallel compression).
   Make-up gain is adaptive: it matches loudness in and out over about 1.5 s, so Amount changes
   density, not level.
   - **Sidechain:** the detector listens either to the chain's own signal (default) or to the
     plugin's **sidechain input** from the DAW (in Logic, the Side Chain menu). Ducking and
     pumping are done in the DAW; there is no internal sound-keyed ducking.
2. **Distortion / saturation:**
   - Character types from warm (tape/tube), through aggressive (clip/fold), to digital
     (crush/decimate), blended continuously.
   - **Clean low end:** a Linkwitz-Riley crossover that keeps the sub intact. The dry bus gets
     the matching allpass, so dry and wet stay in phase at any Chain amount (phase only; level
     and tone are unchanged).
   - **Exciter:** a high-band control inside this stage, sharing the distortion engine.
3. **Filter/EQ:** high-pass and low-pass, plus broad low/mid/high bands.
4. **Output level.**

**Precise control:** every stage has its own controls on the KIT page (compressor amount,
attack, release, mix, sidechain source; distortion drive, type blend, exciter amount and tone,
low-end crossover; HP, LP and low/mid/high EQ; output level). Each stage has a **Follow XY**
toggle (on by default); off, the stage stays exactly where its controls are.

Reverb, delay and stereo/spatial effects are left to the DAW.

## 5. XY pad

The main way the chain is operated: one gesture moves the whole chain.

- **Y = heat** (clean → destroyed): raises drive, compression and exciter together, and
  shortens the compressor attack so transients are caught rather than spiking.
  **Automatic gain compensation** keeps loudness roughly steady, so heat changes character, not
  volume. The clean-low-end crossover rises with heat, so the sub stays solid.
- **X = character** (warm → aggressive → digital): blends the distortion types continuously;
  the exciter's tone follows.
- **Offsets, like the FM macros:** the stage controls set a base; the XY pushes the stages around
  it. It never overwrites them, and each affected control shows a lime marker at its
  effective value. X and Y are automatable host parameters.
- Positions can be stored per sequencer step (see §8) and moved by the modulators (§6).
- **Chain scenes:** four stored chain states (A–D) and a morph between two of them.

## 6. Modulators (×2, kit level)

- **Shape:** drawn with points and curves.
- **Modes:** tempo-synced loop, fire once per trigger, or free-running.
- **Routing:** drag onto any parameter, with a depth per target.
- **Smooth/humanize:** an amount per modulator.
- Values a modulator is moving show in lime, like XY and macro offsets (§12).

## 7. Guided variations (per sound)

- **Vary** generates about 4 alternatives close to the current sound. An **amount** control sets
  how far they stray.
- **Directions:** brighter/darker, shorter/longer, tonal/noisy, cleaner/dirtier.
- **Locks** by section: source, FX, envelopes.
- Each suggestion shows a waveform and a short note of what changed (e.g. "decay −41% · op2
  +6%"). Hold one to hear it.
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
- **Pattern keys:** MIDI notes C3–D#4 (60–75) play patterns 1–16 **while held**; releasing stops
  the pattern, and pressing another pattern key switches. A **Latch** switch keeps it running
  after release. Starts are quantised to the next step, beat or bar (a setting), and follow the
  host tempo (internal tempo when the host is stopped). The key's velocity scales the pattern's
  dynamics. One-shot hits on C1–G1 still play on top of a running pattern.
- **Default patterns:** during development pattern 1 holds a breakbeat demo. **The release
  build ships with every pattern empty**; factory patterns come through the library (§10, §11).
- **Excluded:** song mode and arrangement (the DAW does these).

## 9. MIDI

A **MIDI mode** switch chooses between the two modes below. In both, **pattern keys** (C3–D#4,
§8) play patterns: on any channel in Drum map mode, and on channel 10 only in Chromatic mode, so
the other channels stay fully chromatic for the Keys Sound.

- **Drum map, any channel** (default): GM notes 36–43 trigger sounds 1–8 on every channel.
- **Chromatic:** every channel except 10 plays one chosen sound, the **Keys Sound**, like a mono
  synth. Channel 10 still uses the drum map. Keys Sound is an automatable parameter that also
  follows the sound selected in the editor.

## 10. Library and presets

**File levels**
- **Sound:** one slot's sound.
- **Kit:** 8 sounds, the kit chain, XY position, scenes and modulators (not the patterns, so kits
  swap under a playing beat).
- **Pattern:** one reusable sequence.
- **Set:** everything (the kit, all 16 patterns, sequencer, MIDI and Master settings).
- Every level has **Init**, **Load**, **Save** and **Save As**; all are undoable.

**Samples**
- Presets reference samples by path.
- **Collect samples** copies them next to the kit so it's portable.
- Missing samples trigger a **search/relink** prompt.

**Library folder**
- On disk (`~/Music/Negative Space/Batida/`), with *Factory* and *User* areas.
- Files are readable XML (`.batida-sound`, `-kit`, `-pattern`, `-set`), so the user can manage
  them in Finder.

**Browser**
- Categories: kick, snare, hat, perc, FX, bass, texture.
- Character tags: warm, harsh, digital, metallic, organic.
- Search and favourites.
- Click to try it live (a run of tries is one undo step), and next/previous while a pattern
  plays, from the LIB view or the ◀ name ▶ strips on other pages.

**Getting samples in**
- Drag from Finder onto a sound.
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

## 12. UI and visual design

Direction **C, "Signal"**, chosen 2026-09-24 from three mocked-up directions. The mockups are
the reference for layout and look.

**Look**
- Pure black ground; white for text and for values the user set; one accent, acid lime
  (`#D4FF3A`).
- **The lime rule:** lime means *something is moving this value*: the XY pad, an FM macro or a
  modulator. White is always the value the user set. This replaces the orange effective-value
  markers and the green modulator colour.
- Type: JetBrains Mono throughout, Martian Mono for headings and names. Section headers are
  numbered, inverted tags in signal-flow order (01 DYNAMICS, 02 DISTORTION…).
- Controls are **horizontal value bars** (label · bar · number) rather than knobs. The number is
  always visible, and drags are relative to the current value (they never jump). A moved value
  shows a lime tick at its effective value, with a lime segment back to the set value.
- Selection is shown inverted (white block, black text). Hard edges and 1 px lines; no
  gradients or textures.
- Everything is drawn as vectors in code, with no bitmaps, so it stays sharp at every zoom and
  on Retina screens.

**Window:** fixed at 980×640, with 100 / 125 / 150% zoom in settings.

**On every page**
- **Top bar:**
  - the wordmark and the page nav (KIT · SEQ · MOD · SOUND · LIB);
  - the kit's ◀ name ▶ strip, and the missing-samples notice when there is one;
  - Undo/Redo;
  - settings (MIDI mode, Keys Sound, zoom);
  - the master meter and level.
- **Sound row:** the 8 slots, each with name, note, hit light, Chain amount, Mute and Solo.
  Clicking one selects and plays it. The row is hidden on SEQ, where the track headers do the
  same job.

**Pages**
- **KIT:**
  - a large XY pad that shows a trail of recent positions;
  - the chain drawn as a signal path (SC in → 01 Dynamics → 02 Distortion → 03 EQ → 04 Output →
    + dry → master), with each stage's Follow XY toggle;
  - scenes A–D as small pads, with the morph.
- **SEQ:**
  - the pattern ◀ name ▶ strip, a map of all 16 pattern slots, tempo, Sync and Play;
  - length, Run, Start on, Latch and Swing;
  - the lane selector, step-page map and Copy/Paste/Clear;
  - the grid: velocity bars, probability and ratchet notes in lime, a lime playhead, and the XY
    lock lane.
- **MOD:** the two modulators side by side. Each has a large shape editor (square points, round
  curve handles, a playhead with its live value), timing (settings that don't apply to the mode
  are dimmed), output and a targets list.
- **SOUND:** one page for the selected sound, laid out as its own signal flow.
  - Header: the sound's ◀ name ▶ strip, the source (Sample / FM / Layer), and Level, Pan,
    Velocity and Chain.
  - Signal path: 01 Source (for FM: the algorithm diagram, pitch, feedback and macros; the
    operators open a larger editor) → 02 Punch → 03 Drive, with its curve → 04 Filter, with its
    curve.
  - Below: the operators, the amp envelope, and pitch envelope and play, with drawn curves.
  - Graphs replace the old help text.
- **Vary:** opens over the SOUND page. It has amount, direction and locks, then the original
  plus 4 suggestion tiles (§7), and Keep, Back and Save to library.
- **LIB:** tabs (sounds, kits, patterns, sets, samples), filters, the list and a details panel
  (§10).

**Words:** everything the user sees says **sound** (§3), including the parameter names shown in
hosts (e.g. *Keys Sound*, *S1 Level*). Parameter IDs don't change.

UI code stays separate from the engine.

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

1. **Sound engine:** FM and sample sources, per-sound FX, envelopes, glide, MIDI, minimal UI.
2. **Kit chain and XY pad** (moved ahead of the sequencer, since the chain is the core):
   dynamics with sidechain input, distortion with exciter and clean low end, filter/EQ, output;
   XY pad with gain compensation and effective-value markers; Follow XY per stage; per-sound
   Chain amount.
3. **Sequencer**, built to play the chain as well as the sounds:
   - patterns and 8–64 steps;
   - velocity, pitch, slice, ratchet and probability lanes;
   - the per-step XY lane;
   - swing;
   - host sync.
4. **Movement and inspiration:** the two drawn modulators, guided variations, and chain scenes.
5. **Library:** sound/kit/pattern files, the browser, tags, sample relinking and collecting.
6. **Visual design** (§12): the C "Signal" look, custom controls, drawn graphs, the page
   layouts, zoom, and the "sound" wording, including the LIB view built in phase 5.
7. **Preset factory:** factory content and the user's listening pass.
8. **VST3 build** and cross-host checks on macOS.
9. **Later:**
   - resonator body (if CPU allows);
   - breeding.

## 15. Out of scope (v1)

- Reverb, delay and spatial effects.
- Time-stretching.
- Song and arrangement mode.
- Wavetable synthesis.
- Sound breeding.
- Polyphony.
- The resonator body (until CPU measurements allow).
- A standalone app.
- Windows.
