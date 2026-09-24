# Negative Space Batida

A sound-design drum instrument (AU, macOS). The design is in [SPEC.md](SPEC.md); the current
phase is described in [docs/PHASE6-PLAN.md](docs/PHASE6-PLAN.md).

**So far:** 8 monophonic sounds, each FM (4 operators, 8 algorithms, macros), Sample, or both
layered, with punch → drive → filter, envelopes and glide (phase 1). A shared **kit chain**
(dynamics → distortion with exciter and clean low end → EQ → output), played as a whole from an
**XY pad**, with a dry/wet Chain amount per sound and a sidechain input (phase 2). A **step
sequencer** with 16 patterns, played by pattern keys or the host transport, with per-step XY
locks and sample slicing (phase 3). **Movement and inspiration**: two drawn modulators, guided
**Vary**, chain scenes with morph, and undo for what Logic can't undo (phase 4). A **library** of
sounds, kits, patterns and sets, with a browser, tags, favourites, Save/Load/Init at every level,
and sample collecting and relinking (phase 5). The **Signal** look: black, mono type, value bars,
and lime for whatever Batida is moving (phase 6).

## Build

Requirements: Xcode and CMake.

```bash
git submodule update --init --depth 1
cmake -B build -G Xcode
cmake --build build --config Release
```

The build installs `~/Library/Audio/Plug-Ins/Components/Batida.component`. After a rebuild,
run `killall -9 AudioComponentRegistrar` so Logic and `auval` see the new version.

The editor embeds JetBrains Mono and Martian Mono (SIL Open Font License 1.1); the fonts and
their licences are in `resources/fonts/`.

## Tests

```bash
build/BatidaTests_artefacts/Release/BatidaTests              # engine unit tests
build/BatidaTests_artefacts/Release/BatidaTests --bench      # CPU, worst-case load
build/BatidaTests_artefacts/Release/BatidaTests --render build/renders   # default kit as WAVs
build/BatidaTests_artefacts/Release/BatidaTests --pad build/pad          # a beat at 5 pad positions
build/BatidaTests_artefacts/Release/BatidaTests --beat build/beat        # the breakbeat (pattern 1)
auval -v aumu Btda Ngsp                                      # Apple's AU validation
swiftc -O -o build/au_check tests/au_check.swift && build/au_check   # installed AU, host-style
build/BatidaSnapshot_artefacts/Release/BatidaSnapshot build/snapshots  # screenshots + gesture checks
```

`au_check` loads the installed AU like a host does. It renders each sound, checks both MIDI
modes and chromatic pitch, feeds the sidechain input the way a host does, and round-trips saved
state, including sample paths and missing files. Host values: knobs are normalised 0–1, menus are indexes (0 = first entry).

## Playing it in Logic

Insert it as an instrument: *AU Instruments → Negative Space → Batida*. The top bar has the pages
(**KIT** pad and chain, **SEQ** sequencer, **MOD** modulators, **SOUND** the selected sound,
**LIB** library), the kit's ‹ name ›, **UNDO / REDO**, **CFG** (MIDI mode, Keys Sound, zoom
100/125/150%, library folder, author) and the master meter and level.

**White is what you set; lime is what Batida is moving** (the pad, FM macros, modulators, step
locks).

- **Value bars** (every continuous control): drag left/right or up/down, always from the current
  value. **Shift** = fine. **Click the number** = type a value. **Double-click** (or Option-click)
  = back to the default.
  **Right-click** = Modulate with Mod 1 / Mod 2, type a value, default. The scroll wheel nudges.
  A lime tick and segment show where Batida has moved the value; a modulated one also shows its
  reach as a dim lime band.
- **XY pad:** X = character (warm → aggressive → digital), Y = heat (clean → destroyed). The white
  square is where you put it; drags are relative, **Shift** locks one axis, **Option-click** puts it
  where you click, double-click goes back to the default. The lime square is where the chain
  really is when a step lock or modulator moves it, with a one-second trail. XY Character and XY
  Heat are automatable.
- **Stages** (01 Dynamics, 02 Distortion, 03 EQ, 04 Output) set the base the pad moves around. The
  **XY** chip off = that stage ignores the pad.
- **Sidechain:** set the Dynamics **Detector** to *Sidechain*, then choose a source track in the
  Side Chain menu at the top of Batida's plugin window. The key ducks the chain; with the key
  silent there's no boost.

- **Sound row** (every page but SEQ): click a slot to select it and play it while held;
  **double-click** to rename; **drag onto another** to swap the two slots (the sound, sample,
  name, mute/solo and its track in every pattern move together; each slot keeps its MIDI note);
  **right-click** for Rename, Swap with, Init / Load / Save sound. **M / S** mute and solo; while
  any sound is soloed, only soloed sounds play. The bars under each name are its Chain amount
  (100% = through the chain, 0% = dry); the light flashes on every hit. On SEQ the track headers
  do all of this.
- **MIDI mode: Drum map** (default). Notes 36–43 (C1–G1) play sounds 1–8 on any channel.
- **MIDI mode: Chromatic.** Every channel except 10 plays the **Keys Sound** like a mono synth,
  with C3 (60) at the sound's own pitch. Channel 10 still plays the drum map, so one instance can
  take a drum track and a bass line. Clicking a slot also makes it the Keys Sound.
- **SOUND page:** 01 Source (FM, Sample or Layer) → 02 Punch → 03 Drive → 04 Filter, then the
  operators (or slices and loop for a sample), amp envelope, and pitch envelope and play. The
  graphs show the envelopes, the filter and the drive curve. **OPERATORS ↗** (or an operator card)
  opens every operator control; lime shows where the macros have moved them.
- **Samples:** drop a WAV/AIFF/FLAC onto a slot (or the window), or use **LOAD…**. A sound on FM
  switches to Sample when you load one. Drag the square handles on the waveform for start and end.
- **Wave** on each operator runs sine > triangle > saw > square > noise. The default hats,
  snare, rim and clap use the noise end through the sound's filter.

### Library

- **Where:** `~/Music/Negative Space/Batida/`, with `Factory` (written by Batida) and `User`
  (yours). Files are readable XML: `.batida-sound`, `.batida-kit`, `.batida-pattern`,
  `.batida-set`. Manage them in Finder as you like.
- **Levels:** a **sound** (one slot), a **kit** (8 sounds, chain, pad, scenes, modulators; not the
  patterns), a **pattern**, and a **set** (everything, including all 16 patterns and the settings).
- **LIB view:** pick Sounds / Kits / Patterns / Sets / Samples; filter by All / Factory / User /
  ♥, category, character and search. **Click a row to try it**, live and in the beat: a sound goes
  into the selected slot (and plays once if nothing is running), a pattern into the current
  pattern. ‹ PREV / NEXT › step through the list. A run of tries is one **Undo**. Click the ♥ to keep a
  favourite. A User file's character can be edited on the right.
- **‹ name › strips:** the kit (top bar), the sound (SOUND page) and the pattern (SEQ page) step
  through the LIB list from any page. **Click the name** for
  **Init**, **Load…**, **Save** and **Save as…**. The kit's menu also has the whole set (Init
  everything / Load / Save), Relink samples, Collect samples and Show library in Finder.
- **Saving** opens a small panel: name, category, character, author, and **Collect samples**
  (copies them into "<name> Samples" next to the file, so it travels). Saving over a name asks
  first. **Save elsewhere…** writes anywhere.
- **Samples tab:** browse the library or your own folders (**Add folder…**); click a file to load
  it into the selected slot.
- **Missing samples:** Batida looks for them by name and size in the library and wherever
  samples were found before. What's still missing shows in the top bar (and a "!" on the slot):
  **Relink…** searches a folder you pick, or locate each file.
- Drop a `.batida-sound` onto a slot, or a kit, pattern or set anywhere on the window.

### Movement and inspiration

- **Modulators (MOD page):** draw a shape (click adds a point, drag moves it, double-click
  deletes, Option-drag bends a segment; **SHAPES** for starting shapes). **Sync** loops on
  Logic's bars (1/32 to 8 bars), **Free** runs in Hz, **One-shot** runs once per hit (any, or one
  sound) or per pattern start. Smooth, Humanise, Amount, Uni/Bi.
- **Assign:** right-click any bar → **Modulate with Mod 1 / Mod 2** (again to remove), or
  **+ ADD TARGET** on the MOD page. Modulation never writes the value, so it doesn't fight Logic's
  automation.
- **Scenes (KIT page):** **STORE** then a tile saves the whole chain and pad; click a tile to
  recall it. **MORPH** blends From → To; it's automatable and a modulator can drive it.
- **Vary (SOUND → VARY):** up to 4 nearby versions of the sound open over the page (Amount = how
  far; Direction steers; Keep as is locks sections). Each tile names its two biggest changes;
  hold one to hear it, **KEEP** it, or **BACK**. Suggestions are level-matched to the original (by
  the loudest 50 ms of the hit), so Keep may also move the sound's **Level**. Silent and
  near-identical results are filtered out.
- **UNDO / REDO** (top bar): pattern edits, names, Vary Keep, scene recall and slot swaps. Single
  knob moves are undone in Logic, so the two histories don't overlap.
- During development Mod 1 gently moves XY Heat over each bar; the release starts empty.

### Patterns

- **Hold C3–D#4** (60–75) to play patterns 1–16; release to stop. **C1–G1** still play the sounds
  on top. In Chromatic mode the pattern keys are on channel 10.
- **Run:** *Keys* (default) plays only while a pattern key is held (or with **Latch**, until
  pressed again). *Transport* plays whenever Logic plays, using the **Pattern** parameter.
- **Start on** Step / Beat / Bar: when a pressed key starts (with Logic playing). With Logic
  stopped, patterns start at once on Batida's **Tempo**.
- **Sync** on: follow the project's tempo and position while Logic plays. Off: always run on
  Batida's own Tempo. **Tempo** drags from its value: the whole number and the decimals drag
  separately; click to type, double-click for 120.
- **Grid** (SEQ page), one bar per page; the **Bar** map shows all 64 steps, click a bar to show
  it. The 16 pattern slots at the top show which hold steps; click one to show it.
  - **Steps:** tap = on/off; drag up/down = velocity from where it was; drag to the bottom = off;
    shift-drag = paint steps on.
  - **Pitch, Slice, Ratchet, Prob:** drag up/down from the current value (Ratchet: tap cycles).
  - Option-click a step = that track's length (polymeter); right-click a track header for copy,
    paste, clear and fill; right-click a bar in the Bar map to copy, paste or clear that bar.
- **XY lock row:** tap a step to lock it to the pad's current position (tap again clears); drag
  to move the lock (up/down = heat, left/right = character). It lasts that step, then the pad
  snaps back. **XY REC** (KIT page, under the pad) records pad moves into the playing steps.
- **Slices:** on a sample sound, set Slices to Grid or Transients (SOUND page, 05 Slices · loop);
  the Slice lane picks which slice each step plays.
- Pattern 1 holds a breakbeat demo during development; the release build ships with every
  pattern empty.

| Slot | Note | Default sound |
|---|---|---|
| 1 | C1 (36) | Kick |
| 2 | C#1 (37) | Rim |
| 3 | D1 (38) | Snare |
| 4 | D#1 (39) | Clap |
| 5 | E1 (40) | Tom |
| 6 | F1 (41) | Bass (Gate mode, glide) |
| 7 | F#1 (42) | Closed hat |
| 8 | G1 (43) | Open hat |
