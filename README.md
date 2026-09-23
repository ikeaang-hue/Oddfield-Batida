# Negative Space Batida

A sound-design drum instrument (AU, macOS). The design is in [SPEC.md](SPEC.md); the current
phase is described in [docs/PHASE4-PLAN.md](docs/PHASE4-PLAN.md).

**So far:** 8 monophonic voices, each FM (4 operators, 8 algorithms, macros), Sample, or both
layered, with punch → drive → filter, envelopes and glide (phase 1). A shared **kit chain**
(dynamics → distortion with exciter and clean low end → EQ → output), played as a whole from an
**XY pad**, with a dry/wet Chain amount per voice and a sidechain input (phase 2). A **step
sequencer** with 16 patterns, played by pattern keys or the host transport, with per-step XY
locks and sample slicing (phase 3). **Movement and inspiration**: two drawn modulators, guided
**Vary**, chain scenes with morph, and undo for what Logic can't undo (phase 4). Minimal
stock-control UI.

## Build

Requirements: Xcode and CMake.

```bash
git submodule update --init --depth 1
cmake -B build -G Xcode
cmake --build build --config Release
```

The build installs `~/Library/Audio/Plug-Ins/Components/Batida.component`. After a rebuild,
run `killall -9 AudioComponentRegistrar` so Logic and `auval` see the new version.

## Tests

```bash
build/BatidaTests_artefacts/Release/BatidaTests              # engine unit tests
build/BatidaTests_artefacts/Release/BatidaTests --bench      # CPU, worst-case load
build/BatidaTests_artefacts/Release/BatidaTests --render build/renders   # default kit as WAVs
build/BatidaTests_artefacts/Release/BatidaTests --pad build/pad          # a beat at 5 pad positions
build/BatidaTests_artefacts/Release/BatidaTests --beat build/beat        # the breakbeat (pattern 1)
auval -v aumu Btda Ngsp                                      # Apple's AU validation
swiftc -O -o build/au_check tests/au_check.swift && build/au_check   # installed AU, host-style
build/BatidaSnapshot_artefacts/Release/BatidaSnapshot build/snapshots  # editor screenshots
```

`au_check` loads the installed AU like a host does. It renders each voice, checks both MIDI
modes and chromatic pitch, feeds the sidechain input the way a host does, and round-trips saved
state, including sample paths and missing files. Host values: knobs are normalised 0–1, menus are indexes (0 = first entry).

## Playing it in Logic

Insert it as an instrument: *AU Instruments → Negative Space → Batida*. It opens on the **KIT**
page (pad and chain); **SEQ** is the sequencer; **MOD** holds the two modulators; **VOICE** edits
the selected sound (its **Vary** tab suggests variations).

- **XY pad:** X = character (warm → aggressive → digital), Y = heat (clean → destroyed). It moves
  the whole chain; orange dots show where each stage knob has been pushed. Double-click resets.
  XY Character and XY Heat are automatable.
- **Stage knobs** set the base the pad moves around. **Follow XY** off = that stage ignores the pad.
- **Chain amount** per voice (Kit page, or the voice's Source tab): 100% = through the chain,
  0% = dry.
- **Sidechain:** set the Dynamics **Detector** to *Sidechain*, then choose a source track in the
  Side Chain menu at the top of Batida's plugin window. The key ducks the chain; with the key
  silent there's no boost.

- **MIDI mode: Drum map** (default). Notes 36–43 (C1–G1) play voices 1–8 on any channel.
- **MIDI mode: Chromatic.** Every channel except 10 plays the **Keys Voice** like a mono synth,
  with C3 (60) at the voice's own pitch. Channel 10 still plays the drum map, so one instance
  can take a drum track and a bass line. Clicking a voice button also makes it the Keys Voice.
- **Voice buttons** select a voice and play it while held. **Double-click** to rename it;
  **drag one onto another** to swap the two slots (the sound, sample, name, mute/solo and its
  track in every pattern move together; each slot keeps its MIDI note); **right-click** for
  Rename and Swap with. The same works on the track names on the SEQ page.
- **M / S** under each voice (and on each SEQ track) mute and solo it. While any voice is
  soloed, only soloed voices sound. Both are automatable.
- **Samples:** drop a WAV/AIFF/FLAC onto a voice button (or the window), or use *Load...*.
  A voice on FM switches to Sample when you load one.
- **Orange dots** on the FM operator knobs show where the macros have moved that operator.
- **Wave** on each operator runs sine > triangle > saw > square > noise. The default hats,
  snare, rim and clap use the noise end through the voice filter.

### Movement and inspiration

- **Modulators (MOD page):** draw a shape (click adds a point, drag moves it, double-click
  deletes, Option-drag bends a segment; **Shape...** for starting shapes). **Sync** loops on
  Logic's bars (1/32 to 8 bars), **Free** runs in Hz, **One-shot** runs once per hit (any, or one
  voice) or per pattern start. Smooth, Humanise, Amount, Unipolar/Bipolar.
- **Assign:** right-click any knob → **Modulate with Mod 1 / Mod 2** (again to remove), or
  **Add target...** on the MOD page. A modulated knob shows a green arc (its reach) and a moving
  dot. Modulation never writes the knob, so it doesn't fight Logic's automation.
- **Scenes (Kit page, under the pad):** **Store** then **A–D** saves the whole chain and pad;
  click a letter to recall it. **Scene morph** blends From → To; it's automatable and a modulator
  can drive it.
- **Vary (VOICE → Vary):** press **Vary** for up to 4 nearby versions of the sound (Amount = how
  far; Direction steers; Keep as is locks sections). Hold a suggestion to hear it, **Keep** it, or
  **Back**. Suggestions are level-matched to the original (by the loudest 50 ms of the hit), so
  Keep may also move the voice's **Level**. Silent and near-identical results are filtered out.
- **Undo / Redo** (header): pattern edits, names, Vary Keep, scene recall and slot swaps. Single
  knob moves are undone in Logic, so the two histories don't overlap.
- During development Mod 1 gently moves XY Heat over each bar; the release starts empty.

### Patterns

- **Hold C3–D#4** (60–75) to play patterns 1–16; release to stop. **C1–G1** still play the voices
  on top. In Chromatic mode the pattern keys are on channel 10.
- **Run:** *Keys* (default) plays only while a pattern key is held (or with **Latch**, until
  pressed again). *Transport* plays whenever Logic plays, using the **Pattern** parameter.
- **Start on** Step / Beat / Bar: when a pressed key starts (with Logic playing). With Logic
  stopped, patterns start at once on Batida's **Tempo**.
- **Sync** on: follow the project's tempo and position while Logic plays. Off: always run on
  Batida's own Tempo. **Tempo** drags from its value: the whole number and the decimals drag
  separately; double-click to type.
- **Grid** (SEQ page), one bar per page; the map beside the lane buttons shows all 64 steps,
  click a bar to show it.
  - **Steps:** tap = on/off; drag up/down = velocity from where it was; drag to the bottom = off;
    shift-drag = paint steps on.
  - **Pitch, Slice, Ratchet, Prob:** drag up/down from the current value (Ratchet: tap cycles).
  - Option-click a step = that track's length (polymeter); right-click a track name for copy,
    paste, clear and fill; right-click a bar in the page map to copy, paste or clear that bar.
- **XY lock row:** tap a step to lock it to the pad's current position (tap again clears); drag
  to move the lock (up/down = heat, left/right = character). It lasts that step, then the pad
  snaps back. **XY Rec** (Kit page, by the pad) records pad moves into the playing steps.
- **Slices:** on a sample voice, set Slice Mode to Grid or Transients (Source tab); the Slice
  lane picks which slice each step plays.
- Pattern 1 holds a breakbeat demo during development; the release build ships with every
  pattern empty.

| Voice | Note | Default sound |
|---|---|---|
| 1 | C1 (36) | Kick |
| 2 | C#1 (37) | Rim |
| 3 | D1 (38) | Snare |
| 4 | D#1 (39) | Clap |
| 5 | E1 (40) | Tom |
| 6 | F1 (41) | Bass (Gate mode, glide) |
| 7 | F#1 (42) | Closed hat |
| 8 | G1 (43) | Open hat |
