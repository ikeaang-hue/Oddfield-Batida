# Negative Space Batida

**Batida** (Portuguese for "beat" and "hit") is a sound-design drum instrument for macOS. Eight
sounds run into one shared **kit chain**, and an **XY pad** plays that chain as a whole. There
are also a step sequencer, drawn modulators, guided variations and a library. It works for any
style but is built for synthetic and heavy music: techno, breakbeat, glitch.

Version **0.6.0**, pre-release. Audio Unit only for now; VST3 comes later. The design is in
[SPEC.md](SPEC.md), and each build phase has a plan in [docs/](docs/).

## What's in it

- **8 sounds**, each FM (4 operators, 8 algorithms, four macros), Sample (slices, loop, reverse)
  or both layered. Each has punch → drive → filter, amp and pitch envelopes, and glide. They
  are monophonic and chromatic.
- **Kit chain:** dynamics (with a sidechain input) → distortion with exciter and clean low end →
  EQ → output. A **Chain amount** per sound mixes it dry or wet.
- **XY pad:** X = character (warm → aggressive → digital), Y = heat (clean → destroyed). One
  gesture moves the whole chain; the stage controls set the base it moves around.
- **Sequencer:** 16 patterns of 8–64 steps, per-track lengths, lanes for velocity, pitch, slice,
  ratchet and probability, per-step XY locks, and swing. Patterns are played by MIDI keys or by
  the host transport.
- **Movement:** two drawn modulators, four chain scenes with a morph, and **Vary**, which
  suggests nearby versions of a sound.
- **Library:** sounds, kits, patterns and sets as readable files, with a browser, tags,
  favourites, and sample collecting and relinking.
- **Signal UI:** black, mono type, value bars. **White is what you set; lime is what Batida is
  moving.** Zoom is 100 / 125 / 150%.

## Build and install

You need macOS 12 or later, Xcode and CMake.

```bash
git submodule update --init --depth 1
cmake -B build -G Xcode
cmake --build build --config Release
```

The build installs `~/Library/Audio/Plug-Ins/Components/Batida.component`. After each rebuild,
run `killall -9 AudioComponentRegistrar`, then quit and relaunch your host so it loads the new
version.

In Logic: *AU Instruments → Negative Space → Batida*.

## Using it

The top bar has the pages (**KIT · SEQ · MOD · SOUND · LIB**), the kit's ‹ name ›, **UNDO /
REDO**, **CFG** (MIDI mode, Keys Sound, zoom, library folder, author) and the master meter and
level. Click **BATIDA/** for the version and credits.

### Gestures

| Control | How |
|---|---|
| Value bar | Drag left/right or up/down, always from the current value. **Shift** = fine. **Click the number** = type a value. **Double-click** (or Option-click) = default. **Right-click** = Modulate with Mod 1 / 2, type, default. The wheel nudges. |
| XY pad | Drag (relative). **Shift** locks one axis. **Option-click** puts it where you click. Double-click = default. |
| Sound slot | Click = select and play. Double-click = rename. Drag onto another slot = swap. Right-click = rename, swap, init / load / save sound. **M / S** = mute / solo. |
| ‹ name › strips | The arrows step through the library; the name opens Init, Load, Save, Save as. |
| Tempo | Drag the whole number or the decimals. Click to type; double-click = 120. |

### Pages

- **KIT:** the XY pad, the chain drawn as a signal path (01 Dynamics, 02 Distortion, 03 EQ, 04
  Output; the **XY** chip lets a stage ignore the pad), scenes A–D with **STORE** and **MORPH**,
  and **XY REC** (records pad moves into the playing pattern). On the pad, the white square is
  where you put it and the lime square is where the chain really is.
- **SOUND:** the selected sound as its own path. 01 Source (FM, Sample or Layer) → 02 Punch → 03
  Drive → 04 Filter, then operators (or slices and loop), amp envelope, and pitch envelope and
  play.
  - **OPERATORS ↗** opens every FM operator control.
  - **VARY** suggests up to 4 versions. Each shows what changed; hold one to hear it, then
    **KEEP** or **BACK**. Suggestions are level-matched to the original.
- **SEQ:** pattern ‹ name › and the 16 slots, tempo, **SYNC**, **PLAY**, length, run mode (Keys /
  Transport), start quantise, **LATCH**, swing, lanes, and the bar map.
  - Grid, in the Steps lane: tap = on/off; drag = velocity (to the bottom = off); Shift-drag =
    paint. Option-click a step = that track's length.
  - **XY LOCK** lane: tap = lock the step to the pad; drag = move the lock.
- **MOD:** two modulators: draw the shape (click adds, drag moves, double-click deletes,
  Option-drag bends), Sync / Free / One-shot, Smooth, Humanise, Amount, and targets. Modulation
  never writes the value, so it doesn't fight host automation.
- **LIB:** sounds, kits, patterns, sets and sample folders, filtered by source, category,
  character and search. **Clicking a row tries it live**; one **UNDO** takes back a whole run of
  tries. Click ♥ to mark a favourite.

### MIDI

| Notes | Plays |
|---|---|
| C1–G1 (36–43) | Sounds 1–8 (Kick, Rim, Snare, Clap, Tom, Bass, Closed hat, Open hat by default) |
| C3–D#4 (60–75) | Patterns 1–16 while held (**LATCH** keeps them running) |

- **Drum map** (default): the notes above work on every channel.
- **Chromatic:** every channel except 10 plays the **Keys Sound** like a mono synth (C3 = the
  sound's own pitch). Channel 10 keeps the drum map and pattern keys.

**Sidechain:** set Dynamics → **Detector** to *Sidechain*, then pick a source in Logic's Side
Chain menu at the top of the plugin window.

### Library and samples

- **Where:** `~/Music/Negative Space/Batida/`, with **Factory** and **User** folders.
- **Files:** readable XML in four types: `.batida-sound`, `.batida-kit` (8 sounds, chain, pad,
  scenes, modulators; not patterns), `.batida-pattern` and `.batida-set` (everything). You can
  manage them in Finder.
- **Saving:** set name, category, character, author, and optionally **Collect samples**, which
  copies them next to the file so it travels.
- **Samples:** drop a WAV, AIFF or FLAC onto a slot. Missing samples are searched for by name
  and size; what's still missing shows in the top bar, where **Relink…** finds them.

## Development

```bash
build/BatidaTests_artefacts/Release/BatidaTests               # engine unit tests
build/BatidaTests_artefacts/Release/BatidaTests --bench       # CPU, worst-case load
build/BatidaTests_artefacts/Release/BatidaTests --render build/renders   # default kit as WAVs
auval -v aumu Btda Ngsp                                       # Apple's AU validation
swiftc -O -o build/au_check tests/au_check.swift && build/au_check     # the installed AU, host-style
build/BatidaSnapshot_artefacts/Release/BatidaSnapshot build/snapshots  # screenshots + gesture checks
```

- `au_check` loads the installed AU the way a host does. Host values: continuous controls are
  0–1, menus are indexes.
- `BatidaSnapshot` renders every page and state (and KIT at each zoom) offscreen, then runs
  gesture checks on the real editor. A third argument adds a paint benchmark.

**Layout:**
- `Source/Engine`: the DSP; no UI.
- `Source/Library`: preset files, library index, sample locating.
- `Source/Plugin`: the AU processor.
- `Source/UI`: the editor. `Look/Theme` holds the palette, fonts and LookAndFeel; `ParamControl`
  is the value bar and its relatives.
- `tests/`: unit tests and tools.

**Before a release:** set `kShipDemoPattern` (`Source/Engine/Sequencer/Pattern.h`) and
`kShipDemoModulation` (`Source/Engine/Movement/Movement.h`) to `false`. In development, pattern 1
holds a breakbeat and Mod 1 moves XY Heat.

## Licence

Batida will be released as free, open-source software under the **GNU AGPL v3**; the licence file
comes with the first public release. It is built with [JUCE](https://juce.com) (AGPLv3). It
embeds **JetBrains Mono** and **Martian Mono** under the SIL Open Font License 1.1; the fonts
and their licences are in `resources/fonts/`.

© 2026 Negative Space
