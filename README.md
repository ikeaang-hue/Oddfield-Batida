# Batida

**A drum instrument for sound design**, from Oddfield. macOS · Audio Unit · version 0.9.0
(pre-release).

Eight sounds play into one shared effects chain. An **XY pad** pushes the whole chain at once:
left to right goes warm → aggressive → digital, bottom to top goes clean → destroyed. Around
that are a step sequencer, two drawn modulators, a "Vary" button that suggests new versions of a
sound, and a library. It works for any style but is made for techno, breakbeat and glitch.

> **Testing Batida?** Start with [TESTING.md](TESTING.md): what to try, known limits, and how to
> report.

---

## Quick start

You need macOS 12 or later, Xcode and CMake (`brew install cmake`).

```bash
git clone --recurse-submodules https://github.com/ikeaang-hue/Oddfield-Batida.git
cd Oddfield-Batida
cmake -B build -G Xcode
cmake --build build --config Release
killall -9 AudioComponentRegistrar
```

Then quit and reopen your host and add Batida as an instrument: in Logic or GarageBand,
**AU Instruments → Oddfield → Batida** on a software-instrument track; in Live, turn on
Audio Units in Settings → Plug-Ins and rescan; in Reaper, it's under AUi in the FX browser.
Note names in this README follow Logic (note 60 = C3); the note numbers are the same everywhere.

To update later: `git pull && git submodule update --init`, then build again and run the
`killall` line.

---

## Your first five minutes

1. **Play C1 to G1** (notes 36–43) on your keyboard. Those eight notes are the eight sounds:
   kick, rim, snare, clap, tom, bass, closed hat, open hat.
2. **Drag the XY pad** on the KIT page while you play. That's the heart of Batida.
3. **Click a sound** in the row at the top, then open the **SOUND** page to change it. Press
   **VARY** for four new versions; hold one to hear it and **KEEP** the one you like.
4. **Open LIB → Patterns** and click *Breakbeat*. **Hold C3** (note 60) to play it.
5. **Made a mistake?** **UNDO** is in the top bar.

---

## How it looks

Everything is black and white. **White is what you set. Lime is what Batida is moving**: the XY
pad pushing a control, a modulator, or an FM macro.

Every value is a **bar**:

| To… | Do this |
|---|---|
| change it | drag left/right (or up/down), always starting from where it is |
| change it finely | hold **Shift** while dragging |
| type an exact value | double-click the number |
| reset it | double-click the bar (or Option-click) |
| modulate it | right-click → *Modulate with Mod 1 / Mod 2* |

---

## The pages

The top bar switches pages. It also holds **UNDO / REDO**, **CFG** (settings and zoom) and the
master level. Click **BATIDA/** for the version.

**KIT: the pad and the chain**
- **XY pad.** The white square is where you put it; a lime square shows where the chain really
  is when something else moves it. **Shift** locks one direction. **Option-click** jumps to a spot.
- **Chain:** 01 Dynamics → 02 Distortion → 03 EQ → 04 Output. Each stage's **XY** chip turns the
  pad's influence on or off.
- **Scenes A–D.** Click **STORE**, then a letter, to save the chain. Click a letter to recall it.
  **MORPH** blends between two scenes.

**SOUND: one sound in detail**
- Pick **FM**, **SAMPLE** or **LAYER** (both) at the top.
- The path runs left to right: Source → Punch → Drive → Filter. Below it are the operators (or
  slices for a sample), the amp envelope and the pitch envelope.
- **OPERATORS ↗** opens every FM setting. **VARY** suggests new versions; switch it to **KIT**
  to vary all eight sounds at once (click a slot number to keep that sound as it is). Holding a
  kit suggestion plays the pattern on screen with it.
- **STACK** (along the bottom of Source): play the sound as 2–4 copies. **Detune** and
  **Spread** make it thick and wide (a reese from one slot); **Interval** puts the copies at an
  octave, a fifth or a chord (Minor, Minor 7, Major), so one note plays a chord stab.
- **CHOKE** (under Pitch · play): sounds in the same group cut each other, like closed and open
  hats. The default kit's hats are in group 1.
- To use a sample, drop a WAV, AIFF or FLAC onto a sound in the top row.

**SEQ: patterns**
- 16 patterns of up to 64 steps. **Hold C3–D#4** (notes 60–75) to play patterns 1–16; **LATCH**
  keeps them going.
- In the grid: click a step to turn it on or off, and drag up or down for velocity.
  **Option-click** a step to make that track shorter.
- The lanes (Pitch, Slice, Ratchet, Prob) change what each step does. The **XY LOCK** row moves
  the pad on a single step. On the KIT page, **XY REC** writes the pad into that row while a
  pattern plays and you hold the pad.
- **VARY** suggests four versions of the pattern: denser, sparser, broken, with ghost notes or
  rolls. Hold one to hear it (even with the sequencer stopped); **KEEP** writes it in.
- **Drop a drum loop onto the grid** to turn it into a kit: its hits go to the kick, snare, hat
  and percussion slots as slices, and the pattern replays the loop at any tempo.
- **MIDI ↗**: drag it onto a track in any DAW to get the pattern as a MIDI region, or click it
  to save the file (`User/MIDI`).

**MOD: two modulators**
- Draw a shape: click to add a point, drag to move it, double-click to delete it. **SHAPES** has
  ready-made ones.
- **+ ADD TARGET**, or right-click any bar, to choose what it moves.

**LIB: the library**
- Browse sounds, kits, patterns and sets. **Clicking one loads it right away**, even while a
  pattern plays. One **UNDO** takes back a whole run of clicks.
- ♥ marks a favourite. Use **SAVE AS…** to keep your own.
- The factory library: about 290 sounds, 40 kits, 58 patterns and 19 sets, all made with Batida's
  own engine (no outside samples). Techno, breakbeat and glitch at the core, plus house, garage,
  hip hop and trap: search a style's name to find its kits, patterns and sets. Four moods, *neutral*,
  *synth-aggressive*, *neon* and *tender*, each have tonal sounds, a kit and a set; search the mood.
- Files live in `~/Music/Oddfield/Batida/`, with *Factory* and *User* folders you can open
  in Finder.

**The sound row** (top of every page; on SEQ, down the left side)
- Click a slot to select and play it. Double-click to rename it; drag it onto another slot to
  swap the two; right-click for more, including **Choke group** and **Resample here** (the
  pattern on screen as a loop, or any sound's hit, rendered through the chain into this slot;
  the WAV goes to `User/Samples/Resampled`).
- **M** mutes and **S** solos. The small bars show how much of the sound goes through the chain.

---

## MIDI

| Notes | What they play |
|---|---|
| C1–G1 (36–43) | the 8 sounds |
| C3–D#4 (60–75) | patterns 1–16, while held |
| CC 16 / CC 17 | the XY pad's X and Y, until you move the pad |

Note names differ between DAWs (note 60 is C3 in Logic and Live, C4 in Reaper, C5 in FL
Studio), so the numbers are the ones to go by. A pattern exported with **MIDI ↗** uses the
notes above on channel 10 and the two CCs for its XY lane, so it plays back from any DAW.

**CFG → MIDI mode → Chromatic** turns Batida into a mono synth. Every channel except 10 plays
one chosen sound across the keyboard, and channel 10 keeps the drum notes.

**Sidechain:** set Dynamics → *Detector* to **Sidechain**, then route a track into Batida's
sidechain input. In Logic that's the Side Chain menu at the top of the plugin window; in other
DAWs, send audio to the plugin's sidechain input.

---

## Files and scripting

Everything Batida saves is a plain text file: sounds, kits, patterns and sets. Every setting is
listed with its range and unit in [docs/REFERENCE.md](docs/REFERENCE.md), so presets can be
written or edited in a text editor or by a script. Files saved into the User folder show up in
LIB straight away, and when the file behind a loaded sound, kit or pattern changes, its name
strip shows a lime ↻ that loads the new version.

The `batida` command-line tool works without a DAW. It's built next to the plugin
(`build/BatidaCli_artefacts/Release/batida`) and also sits inside it, at
`Batida.component/Contents/Helpers/batida`:

```bash
batida render my.batida-set --pattern 1 --bars 4 -o beat.wav
batida analyze beat.wav
batida validate my.batida-kit
```

`batida help` lists every command (`describe`, `new`, `list` and `params` as well). Add `--json`
to any command for output another program can read.

---

## For developers

<details>
<summary>Tests, tools and code layout</summary>

```bash
build/BatidaTests_artefacts/Release/BatidaTests                        # unit tests
build/BatidaTests_artefacts/Release/BatidaTests --bench                # CPU, worst case
build/BatidaSnapshot_artefacts/Release/BatidaSnapshot build/snapshots  # screenshots + gesture checks
auval -v aumu Btda Odfd                                                # Apple's AU validation
swiftc -O -o build/au_check tests/au_check.swift && build/au_check     # the installed AU, host-style
```

- **CI:** `.github/workflows/tests.yml` runs the unit tests, the snapshot gesture checks and
  auval on macOS for every push to `main` and every pull request. Every check exits non-zero
  when it fails.
- **The reference:** `docs/REFERENCE.md` is generated from the parameter tables
  (`Source/Library/Reference.cpp`); the tests fail when it's out of date, and
  `BATIDA_WRITE_GOLDEN=1` rewrites it. The `batida` tool is in `tools/cli`.
- **Compatibility:** `tests/golden/parameter-ids.txt` lists every parameter ID a release has
  shipped; the tests fail if one changes or disappears (add new ones on purpose, with
  `BATIDA_WRITE_GOLDEN=1`). `tests/fixtures/` holds projects saved by old releases, which must
  keep opening.
- **Demo defaults:** builds start empty, like a release. `cmake -B build -DBATIDA_DEMO=ON` puts a
  breakbeat in pattern 1 and has Mod 1 move the pad. The tests pass either way.
- **Factory library:** `BatidaFactory` makes it from the engine (SPEC §11). `BatidaFactory
  candidates` writes sound candidates to the library's `Review` folder; a review build
  (`-DBATIDA_REVIEW=ON`) shows them in LIB under REVIEW, with KEEP and REJECT (K / R).
  `BatidaFactory build` then assembles the kits, patterns and sets from what was kept and writes
  `resources/factory`, which is zipped into the plugin. Recipes are in `tools/factory`.
- **Code:**
  - `Source/Engine`: the sound (no UI);
  - `Source/Library`: preset files and samples;
  - `Source/Plugin`: the AU;
  - `Source/UI`: the editor (`Look/Theme` holds colours, fonts and styles);
  - `tests/`: tests and tools;
  - `tools/factory`: the preset factory (recipes, kits, patterns);
  - `tools/cli`: the `batida` command-line tool.
- The design is in [SPEC.md](SPEC.md).

</details>

---

## Licence

Free and open source under the **GNU AGPL v3** ([LICENSE.md](LICENSE.md)). Built with [JUCE](https://juce.com). The fonts, JetBrains Mono and Martian Mono, are
under the SIL Open Font License; you'll find them in `resources/fonts/`.

© 2026 Oddfield
