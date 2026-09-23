# Negative Space Batida

A sound-design drum instrument (AU, macOS). The design is in [SPEC.md](SPEC.md); the current
phase is described in [docs/PHASE1-PLAN.md](docs/PHASE1-PLAN.md).

**Phase 1 (voice engine):** 8 monophonic voices, each FM (4 operators, 8 algorithms, macros),
Sample, or both layered. Each voice runs through punch → drive → filter, with amp and pitch
envelopes and glide. MIDI in and a minimal stock-control UI. No sequencer or kit chain yet.

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
auval -v aumu Btda Ngsp                                      # Apple's AU validation
swiftc -O -o build/au_check tests/au_check.swift && build/au_check   # installed AU, host-style
build/BatidaSnapshot_artefacts/Release/BatidaSnapshot build/snapshots  # editor screenshots
```

`au_check` loads the installed AU like a host does. It renders each voice, checks both MIDI
modes and chromatic pitch, and round-trips saved state, including sample paths and missing
files. Host parameters are normalised 0–1.

## Playing it in Logic

Insert it as an instrument: *AU Instruments → Negative Space → Batida*.

- **MIDI mode: Drum map** (default). Notes 36–43 (C1–G1) play voices 1–8 on any channel.
- **MIDI mode: Split.** Channel 10 is the drum map; channels 1–8 play voices 1–8 chromatically,
  with C3 (60) at the voice's own pitch.
- **Voice buttons** select a voice and play it while held.
- **Samples:** drop a WAV/AIFF/FLAC onto a voice button (or the window), or use *Load...*.
  A voice on FM switches to Sample when you load one.
- **Orange dots** on the FM operator knobs show where the macros have moved that operator.

| Voice | Note | Default sound |
|---|---|---|
| 1 | C1 (36) | Kick |
| 2 | C#1 (37) | Rim |
| 3 | D1 (38) | Snare |
| 4 | D#1 (39) | Tom |
| 5 | E1 (40) | Zap |
| 6 | F1 (41) | Bass (Gate mode, glide) |
| 7 | F#1 (42) | Closed hat |
| 8 | G1 (43) | Open hat |
