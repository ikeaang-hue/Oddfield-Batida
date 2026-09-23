# Negative Space Batida

A sound-design drum instrument (AU, macOS). The design is in [SPEC.md](SPEC.md); the current
phase is described in [docs/PHASE1-PLAN.md](docs/PHASE1-PLAN.md).

**So far:** 8 monophonic voices, each FM (4 operators, 8 algorithms, macros), Sample, or both
layered, with punch → drive → filter, envelopes and glide (phase 1). A shared **kit chain**
(dynamics → distortion with exciter and clean low end → EQ → output), played as a whole from an
**XY pad**, with a dry/wet Chain amount per voice and a sidechain input (phase 2). Minimal
stock-control UI. No sequencer yet.

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
auval -v aumu Btda Ngsp                                      # Apple's AU validation
swiftc -O -o build/au_check tests/au_check.swift && build/au_check   # installed AU, host-style
build/BatidaSnapshot_artefacts/Release/BatidaSnapshot build/snapshots  # editor screenshots
```

`au_check` loads the installed AU like a host does. It renders each voice, checks both MIDI
modes and chromatic pitch, feeds the sidechain input the way a host does, and round-trips saved
state, including sample paths and missing files. Host values: knobs are normalised 0–1, menus are indexes (0 = first entry).

## Playing it in Logic

Insert it as an instrument: *AU Instruments → Negative Space → Batida*. It opens on the **KIT**
page; **VOICE** edits the selected sound.

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
- **Voice buttons** select a voice and play it while held.
- **Samples:** drop a WAV/AIFF/FLAC onto a voice button (or the window), or use *Load...*.
  A voice on FM switches to Sample when you load one.
- **Orange dots** on the FM operator knobs show where the macros have moved that operator.
- **Wave** on each operator runs sine > triangle > saw > square > noise. The default hats,
  snare, rim and clap use the noise end through the voice filter.

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
