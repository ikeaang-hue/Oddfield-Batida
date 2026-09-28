# Oddfield Batida: Design Spec

*Version 1.15, agreed 2026-09-28 (1.15: a fuller chain for sound design: filter envelope and comb/formant modes per sound, a Movement stage, Delay and Reverb sends, the SPACE page (phase 9); 1.14: the publisher is renamed Oddfield; 1.13: Stack, 1–4 copies of a sound's source; 1.12: Vary searches only audible settings, measures longer and by spectrum, and relaxes step by step; 1.11: choke groups, Kit and Pattern Vary, resampling, breaks into kits, MIDI out and the XY CCs (phase 8), any-DAW rule; 1.1: MIDI mode, macro behaviour, test host; 1.2: Chromatic mode replaces Split; 1.3: noise waveform, 2× FM; 1.4: the chain as the core, XY operation, per-sound Chain amount, sidechain input, chain before sequencer; 1.5: details settled while building phase 2; 1.6: pattern keys; 1.7: library details and the Set level; 1.8: visual design (direction C, "Signal"), "sound" replaces "voice"; 1.9: the factory library; 1.10: heat crushes, not squashes)*

## 1. Identity

- **Publisher:** Oddfield (manufacturer code `Odfd`, bundle ID `com.oddfield.batida`). It was
  called Negative Space until 0.8.4; the library and settings move from the old folders by
  themselves.
- **Plugin:** Batida (plugin code `Btda`). Portuguese for "beat" and "hit".
- **What it is:** a sound-design instrument, drums first, with basses, stabs and textures in
  scope. It works for general use, but is strongest on synthetic and heavy genres such as
  techno, breakbeat, drum & bass and glitch.
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
sound → [Body: deferred] → punch → drive → filter (+ env) → level/pan ─┬─ dry (1 − Chain amount) ──────────────────┐
                                                                       ├─ wet (Chain amount) ──→ Kit chain ────────┤
                                                                       ├─ Delay send ──→ Delay ──┬─────────────────┤
                                                                       └─ Reverb send ─→ Reverb ←┘ Delay → Reverb ─┤
                                                                                                                   + → master (safety clip)
Kit chain (wet sum of all sounds):
   Dynamics → Distortion (+ exciter, clean low end) → Filter/EQ → Movement → Output
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

### Stack (every source)
- **What it is:** the source runs as 1–4 copies (default 1).
  - A copy is the whole FM source (all four operators and their envelopes) and/or the sample
    player.
  - The copies share everything after the source: amp and pitch envelopes, glide, punch, drive,
    filter, level, pan and chain. So the sound stays monophonic, and glide, choke and Vary work
    as before.
- **It does two jobs:**
  - **Thickness:** detuned copies, a reese from one slot, with all four operators still free for
    FM.
  - **Chords:** copies at an interval, with every operator still free for timbre.
- **Controls:**
  - **Stack:** 1 / 2 / 3 / 4.
  - **Interval:** Unison, Octave, Fifth, Minor, Minor 7 or Major. The copies take the shape's
    notes in order: 0 · 3 · 7 · 10 for Minor 7, 0 · 12 · 0 · 12 for Octave. Two copies of Minor
    are a minor third; four are the triad and its octave.
  - **Detune** (0–100 cents): the copies spread evenly across ±Detune.
  - **Spread** (0–100%): the copies spread evenly across the stereo field. The root sits nearest
    the centre, so a chord stays anchored and in tune.
- **Level:** the copies start at different phases and add up in power, so a stacked sound stays
  about as loud as one copy (within 2 dB).
- **Mono and cost:** with one copy the sound is exactly as before (mono, the same code). Each
  copy adds the cost of its source: with two sounds at 4 copies, the kit's worst case goes from
  6.8% to 8.4% of one core.
- **Automation and Vary:** Detune and Spread can be automated and modulated. Vary leaves Stack
  alone.
- **Width:** Spread is width inside a sound, not a stereo effect; the stereo effects are the
  kit's Movement stage and the sends (§4).

### FX (per sound)
- **Punch:** one transient/compression control.
- **Drive:** amount, plus type (soft, hard, fold, crush).
- **Filter:** cutoff and resonance, in one of six modes. The two controls mean what the mode
  needs:
  - **LP, HP, BP:** as before.
  - **Notch:** cuts a band (resonance = how narrow).
  - **Comb:** cutoff is the comb's pitch and resonance its feedback (positive or negative), from
    flanged and metallic to a ringing pitched tone. With Key at 100% the comb is tuned to the
    note, which covers much of what the deferred Body would do.
  - **Formant:** cutoff moves through the vowels (A E I O U) and resonance sets how sharp they
    are.
- **Filter envelope:** Amount (± 4 octaves, 0 by default), attack, decay, sustain and release,
  triggered with the amp envelope. **Key** (0–100%) makes the cutoff follow the note. On Comb and
  Formant the envelope sweeps the pitch or the vowel.
- **Chain amount** (replaces the Clean toggle): a per-sound dry/wet crossfade into the kit
  chain. 100% (default) = fully through the chain; 0% = dry, bypassing it; in between, both are
  heard. Taken after the sound's level and pan. Automatable, so a sound can move in and out.
- **Delay send** and **Reverb send** (0–100%, 0 by default): how much of the sound goes to the
  kit's two send effects (§4). Taken after level and pan, alongside Chain amount and independent
  of it, so a snare can be dry in the chain and still get space, and a bass can stay dry.
  Muting a sound mutes its sends; a choke cuts the sound but its tails ring on.

### Envelopes and play
- **Envelopes:** amp, pitch and filter. FM operators have their own envelopes as well.
- **Monophonic:** one note at a time per sound; a new note retriggers.
- **Glide:** one control per sound, for bass lines and slides.
- **Choke group:** Off or 1–4. A hit cuts the other sounding sounds in its group with the same
  ~1.5 ms fade a sound switch uses (no click). The default kit's hats (slots 7 and 8) are in
  group 1, and the preset factory puts hat pairs in group 1.
- **Range:** sounds are chromatic and can sustain. They lean percussive, but basses,
  stabs and textures are in scope.

### Resampling
- **Resample here** (a slot's menu) renders into that slot, through the whole chain:
  - the pattern on screen: one pass that loops seamlessly (the second of two passes, so the
    first pass's tails are heard at its start), at the current tempo;
  - or any slot's sound: one hit, until it has rung out (at most 6 s).
- The render is a stereo 24-bit WAV in `User/Samples/Resampled`, so the sound can be saved and
  found again. The slot becomes a plain one-shot sample sound with Chain 0% and both sends at 0
  (the chain and the sends are already in it); Pan, Mute and Solo stay. One undo step.

### Body / resonator (deferred)
- The architecture keeps a slot between source and chain.
- The filter's Comb mode, keyed to the note, covers tuned resonance until then.
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
4. **Movement** (1.15): one stage for modulated time effects, in stereo.
   - **Type:** one continuous control, chorus → flanger → phaser, blended like the distortion
     types.
   - **Rate:** synced to the tempo (8 bars to 1/16), or free in Hz with Sync off.
   - **Depth**, **Feedback** (±, for the flanger and phaser) and **Mix** (0% by default, so kits
     made before 1.15 sound the same).
   - Below the distortion's clean-low-end frequency the signal passes untouched, so the sub stays
     mono and solid.
5. **Output level** (the chain's own level).

The dry sounds, the chain and the send returns meet after this, then go through the master level
and the **safety clip** (a soft ceiling just under 0 dBFS, on by default; the CLIP switch).

**Precise control:** every stage has its own controls on the KIT page (compressor amount,
attack, release, mix, sidechain source; distortion drive, type blend, exciter amount and tone,
low-end crossover; HP, LP and low/mid/high EQ; Movement type, rate, depth, feedback and mix;
output level). Each stage has a **Follow XY** toggle; off, the stage stays exactly where its
controls are. Dynamics, Distortion and EQ follow by default; Movement and the sends don't (§5).

### Sends (1.15)

Two send effects at kit level, fed by every sound's Delay and Reverb sends (§3). Both are
stereo. Their returns join the dry sounds and the chain before the master level. Their controls
are on the SPACE page (§12).

**Delay**
- **Time:** synced to the tempo, 1/32 to 1 bar, straight, dotted or triplet. It follows the
  host's tempo, or the internal one when the host is stopped.
- **Feedback** (0–100%) and **Ping-pong** (0% = echoes in place, 100% = full left/right).
- **In the loop:** Low cut, High cut and Drive, so repeats darken and roughen as they go. Low cut
  starts at 100 Hz, so echoes don't muddy the sub.
- **Freeze:** holds what's in the delay and loops it (input off, feedback full) until released.
  An automatable switch, so it can be played from the DAW or a modulator.
- **→ Reverb:** how much of the delay's output also goes into the Reverb.
- **Return** level.

**Reverb**
- **Type:** one continuous control, room → plate → hall.
- **Decay**, **Pre-delay** and **Damping**.
- **Low cut**, starting at 200 Hz, so the sub stays dry.
- **Gate:** Off, or 30–600 ms: the tail closes that long after its input falls quiet (gated
  snares).
- **Duck:** the tail ducks under its own input, so hits stay clear and the tail blooms between
  them. This listens only to the reverb's input; ducking one sound under another is still done in
  the DAW (Dynamics, above).
- **Return** level.

Stereo imaging beyond this (wideners, imagers, panners) is left to the DAW.

## 5. XY pad

The main way the chain is operated: one gesture moves the whole chain.

- **Y = heat** (clean → destroyed): destroys by **crushing, not squashing**. The weight is on the
  distortion (full drive at the top) and the exciter; compression rises only a little, with a
  slightly *longer* attack so kick transients punch through. The clean-low-end crossover rises
  a little, so the sub stays solid while a kick's body is crushed.
  **Gain compensation** keeps the distortion level, and a small output lift makes up for the
  loudness that clipping costs a kick. A destroyed kit ends about as loud as a clean one, or a
  touch louder, never muted.
- **X = character** (warm → aggressive → digital): blends the distortion types continuously;
  the exciter's tone follows.
- **Offsets, like the FM macros:** the stage controls set a base; the XY pushes the stages around
  it. It never overwrites them, and each affected control shows a lime marker at its
  effective value. X and Y are automatable host parameters.
- **Movement and the sends** (1.15) have Follow XY too, **off by default**. On, X moves their
  tone and Y their amount:
  - Movement: X blends chorus → flanger → phaser (warm → aggressive → digital); Y raises depth
    and feedback.
  - Delay: X opens the loop's high cut; Y raises feedback and loop drive (capped at 95%, so only
    Freeze holds forever).
  - Reverb: X lightens the damping; Y lengthens the decay.
- Positions can be stored per sequencer step (see §8) and moved by the modulators (§6).
- **Chain scenes:** four stored chain states (A–D) and a morph between two of them. Scenes hold
  Movement, Delay and Reverb as well (not Freeze).

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
- The filter envelope's amount and times vary under *envelopes*. Vary leaves the filter mode, the
  sends and Chain amount alone (like Stack), so a suggestion stays the same kind of sound in the
  same place in the mix.
- Each suggestion shows a waveform and a short note of what changed (e.g. "decay −41% · op2
  +6%"). Hold one to hear it.
- **Automatic dud filtering:** silent, clipping and near-duplicate results never reach the user.
- **How it searches** (1.12): only settings the sound can hear are varied (no FM settings on a
  sample, no sample settings on FM, nothing on a silent operator), and on a sample, Tonal/Noisy
  use drive, cutoff and resonance. Each try is measured for up to 4 s (so long sounds can get
  longer) for loudness, length, brightness (the spectral centroid) and the spectrum's shape in
  12 bands (so timbre changes count even at the same brightness). When a pass finds too little,
  the next reaches further (1.6× the amount), then asks less (smaller moves count; up to 3 dB
  of level shortfall). There is no message explaining an empty result.
- **History:** full undo/redo, and one click saves a result to the library.
- **Kit Vary:** the same panel switched from SOUND to KIT varies every sound at once, in one
  direction, with the same section locks, plus a lock per slot. Each of the 4 suggestions is a
  whole kit (each sound level-matched); its tile shows the eight sounds side by side. Holding a
  tile plays the pattern on screen with the suggestion in place (or the selected sound, if the
  pattern is empty). KEEP is one undo step; SAVE KIT… saves it.
- **Pattern Vary** (VARY on the SEQ page): 4 suggestions for the pattern on screen.
  - Directions: any, denser, sparser, broken (hits moved off the grid), ghosts (quiet hits
    around the snares and percussion), rolls (ratchets, and a fill on the last beat).
  - Amount, and a lock per track. Tracks with no hits stay empty; the XY lane is kept.
  - Guided by the kit layout: the kick on one stays, hats get busier, the bass is left alone
    except by sparser and broken.
  - Tiles draw the grid, with changed steps in lime and removed ones as outlines. Holding a
    tile plays the suggestion, even with the sequencer stopped (as if its pattern key were
    held). The real pattern changes only on KEEP (one undo step).
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
- **XY REC** (KIT page, off by default, not saved): while a pattern plays and the pad is held,
  every step the playhead passes gets the pad's position in the XY lane, moving or not; steps
  skipped between two screen frames get it too. One undo step per gesture.
- **Drop a break, get a kit:** a drum loop dropped on the SEQ grid.
  - The loop is taken to be 1, 2 or 4 bars of 4/4, whichever puts the tempo nearest 125 bpm
    (within 70–190); the internal tempo is set to it.
  - Its 31 strongest onsets become Transients slices (the most a slot holds); each hit is
    heard as a kick, snare, closed or open hat, or percussion (low, bright and high energy, how
    long it rings, and how tonal it is).
  - The loop goes into slots 1 (kick), 2 (percussion), 3 (snare), 7 (closed hat) and 8 (open
    hat), whichever are used, in Transients mode, one-shot, each slice playing to its end with a
    4 ms fade; the hats choke each other. One decoded copy is shared by all of them.
  - The pattern on screen gets every hit on its 16th, playing its own slice, so the break
    replays at any tempo without stretching. One undo step.
- **MIDI out (any DAW):** the pattern as a standard MIDI file.
  - Drag **MIDI ↗** onto a track in any DAW (a plain file drag), or click it to save the file to
    `User/MIDI` and show it in Finder (for hosts that block dragging out of a plugin window).
  - Slots 1–8 are notes 36–43 on channel 10 (the drum map, which answers in both MIDI modes).
  - Timing is in beats (960 ticks) with no tempo, so the region lands on the grid at the song's
    tempo; swing is written in; one pass of the pattern, as the sequencer plays it (polymeter
    included).
  - Ratchets are written out as notes. Probability is rolled once (the same repeatable roll
    Batida's own playback makes).
  - The XY lane becomes CC 16 (X) and CC 17 (Y); where a lock ends, they return to the pad.
  - Not in the file: the pitch and slice lanes.
- **Excluded:** song mode and arrangement (the DAW does these).

## 9. MIDI

A **MIDI mode** switch chooses between the two modes below. In both, **pattern keys** (C3–D#4,
§8) play patterns: on any channel in Drum map mode, and on channel 10 only in Chromatic mode, so
the other channels stay fully chromatic for the Keys Sound.

- **Drum map, any channel** (default): GM notes 36–43 trigger sounds 1–8 on every channel.
- **Chromatic:** every channel except 10 plays one chosen sound, the **Keys Sound**, like a mono
  synth. Channel 10 still uses the drum map. Keys Sound is an automatable parameter that also
  follows the sound selected in the editor. Changing it releases only the old Keys Sound's
  notes, and only in Chromatic mode.
- **XY CCs:** CC 16 moves X and CC 17 moves Y, on any channel, so exported patterns (§8) play
  their XY lane back from any DAW. The CCs hold the pad until the pad (or its automation) moves;
  a step's XY lock still wins while it lasts.

## 10. Library and presets

**File levels**
- **Sound:** one slot's sound.
- **Kit:** 8 sounds, the kit chain, the Delay and Reverb, XY position, scenes and modulators (not the patterns, so kits
  swap under a playing beat).
- **Pattern:** one reusable sequence.
- **Set:** everything (the kit, all 16 patterns, sequencer, MIDI and Master settings).
- Every level has **Init**, **Load**, **Save** and **Save As**; all are undoable.

**Samples**
- Presets reference samples by path.
- **Collect samples** copies them next to the kit so it's portable.
- Missing samples trigger a **search/relink** prompt.

**Library folder**
- On disk (`~/Music/Oddfield/Batida/`), with *Factory* and *User* areas.
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

- **Size:** the first target was about 160 sounds, 22 kits, 22 patterns and 8 sets. Factory 14
  (under review) has about 290 sounds, 40 kits, 58 patterns and 19 sets.
- **Styles:** techno, breakbeat and glitch at the core, with the most kits; house, garage,
  hip hop and trap as well. Kits, patterns and sets carry their style as a tag, so search finds
  them. There is no separate style filter.
- **Moods:** four more tags, *neutral*, *synth-aggressive*, *neon* and *tender*, each with its own
  tonal sounds (built from tones, not noise), a kit and a set.
- **Method:** an offline tool (`BatidaFactory`) built on the same engine.
  1. Archetype recipes per category.
  2. Guided variations around each archetype.
  3. Render, measure and auto-tag each sound, discarding duds.
  4. Assemble kits from sounds with matching character.
  5. The user listens and keeps or rejects each result in a Review area of LIB (dev builds
     only), in Logic, while a pattern plays.
- **Samples:** 100% rendered from the engine itself. The factory contains no third-party or
  CC0 audio.
- **Slot layout:** the neutral kit's layout is the reference (1 kick · 2 rim/perc · 3 snare ·
  4 clap · 5 perc/tom · 6 bass · 7 closed hat · 8 open hat). A kit changes a slot only where its
  style needs to, and the replacement keeps the slot's rough job, so patterns cross over.
- **Levels:** sounds are matched by their loudest 50 ms, with offsets per category; kits are
  matched at the output across the XY pad.

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
  - the wordmark and the page nav (KIT · SPACE · SEQ · MOD · SOUND · LIB);
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
  - the chain drawn as a signal path (SC in → 01 Dynamics → 02 Distortion → 03 EQ → 04 Movement →
    05 Output → + dry + sends → master), with each stage's Follow XY toggle;
  - the stages in a 2 × 2 grid (Dynamics, Distortion, EQ, Movement), with Output (chain level
    and CLIP) as one row below them;
  - scenes A–D as small pads in one row, with the morph.
- **SPACE** (1.15): the sends.
  - A signal path along the top (sends → 01 Delay → 02 Reverb → + → master).
  - **Sends:** a Delay row and a Reverb row of value bars, one per slot, lined up under the sound
    row, so the whole kit's space reads at a glance.
  - **01 Delay:** a drawn graph of the echoes (taps fading and alternating left/right, darkening
    with the loop's filter), the time as note chips (1/32 … 1 bar, with straight · dotted ·
    triplet), then the bars, FREEZE, → Reverb and Return, and Follow XY.
  - **02 Reverb:** a drawn graph of the tail (pre-delay gap, decay, the gate's cut), then the bars
    and Return, and Follow XY.
- **SEQ:**
  - the pattern ◀ name ▶ strip, a map of all 16 pattern slots, tempo, Sync and Play;
  - length, Run, Start on, Latch and Swing;
  - the lane selector, step-page map, VARY, MIDI ↗ and Copy/Paste/Clear;
  - the grid: velocity bars, probability and ratchet notes in lime, a lime playhead, and the XY
    lock lane.
- **MOD:** the two modulators side by side. Each has a large shape editor (square points, round
  curve handles, a playhead with its live value), timing (settings that don't apply to the mode
  are dimmed), output and a targets list.
- **SOUND:** one page for the selected sound, laid out as its own signal flow.
  - Header: the sound's ◀ name ▶ strip, the source (Sample / FM / Layer), and Level, Pan,
    Velocity, Chain, Delay and Reverb (three columns of two).
  - Signal path: 01 Source (for FM: the algorithm diagram, pitch, feedback and macros; the
    operators open a larger editor; Stack along its bottom for every source) → 02 Punch → 03 Drive, with its curve → 04 Filter, with its
    curve (LP · HP · BP · NOTCH · COMB · FORM chips; Cutoff, Reso, Env and Key; the curve shows
    the comb's teeth and the vowel's peaks, and a lime ghost where the envelope takes it).
  - Below: the operators, the envelopes (06 ENV, with AMP | FILTER tabs over one drawn editor),
    and pitch envelope and play, with drawn curves.
  - Graphs replace the old help text.
- **Vary:** opens over the SOUND page. It has a SOUND | KIT switch, amount, direction and locks
  (per slot for KIT), then the original plus 4 suggestion tiles (§7), and Keep, Back and Save
  to library. Pattern Vary opens the same way over the SEQ page.
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
- **Any DAW:** Batida must not fit only one host. Features use what every host supports
  (parameters, MIDI notes and CCs, standard MIDI files, file drags), and where one host needs
  something special (Logic runs plugins out of process) there is a fallback that works
  everywhere. Docs give note numbers as well as names, since hosts name octaves differently.
- **Old files sound the same:** every setting added in 1.15 starts neutral (sends 0, Movement
  mix 0, filter envelope amount 0, the filter in its old mode), so projects and library files
  from earlier versions load and play unchanged.

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
8. **Sampling and variation** (from the idea board, 2026-09-27): choke groups, Kit Vary,
   Pattern Vary, resampling, breaks into kits, MIDI out with the XY CCs, and XY REC on every
   step.
9. **Sound-design chain** (1.15): the filter envelope, Key and the Notch, Comb and Formant modes;
   the Movement stage; the Delay and Reverb sends with per-sound send amounts; the SPACE page and
   the KIT and SOUND page changes (§12). Mockups first. Still an option for later: a **Repeat**
   stage (beat-repeat), alongside the stutter keys and Throw ideas.
10. **VST3 build** and cross-host checks on macOS (Live, Reaper, Bitwig as well as Logic).
11. **Later:**
   - resonator body (if CPU allows);
   - breeding.

## 15. Out of scope (v1)

- Stereo imaging beyond Movement and the sends (wideners, imagers, auto-pan).
- Time-stretching.
- Song and arrangement mode.
- Wavetable synthesis.
- Sound breeding.
- Polyphony.
- The resonator body (until CPU measurements allow).
- A standalone app.
- Windows.
