# Testing Batida

Batida 0.9.1 is a pre-release. This page lists what to try, what's known not to work yet, and how
to report what you find.

## What you need

- A Mac with macOS 12 or later, Apple Silicon or Intel.
- A host that loads Audio Units: Logic Pro, GarageBand, Ableton Live, Reaper, and others.
  Hosts that load only VST3 or CLAP get Batida with the VST3 build (phase 11).

## Install

In Terminal:

```bash
curl -fsSL https://raw.githubusercontent.com/ikeaang-hue/Oddfield-Batida/main/install.sh | bash
```

Then quit and reopen your host, and add Batida as an instrument:

| Host | Where |
|---|---|
| Logic Pro, GarageBand | a software-instrument track → AU Instruments → Oddfield → Batida |
| Ableton Live | Settings → Plug-Ins: Audio Units on, then Rescan; Batida is under Plug-Ins → Audio Units |
| Reaper | Preferences → Plug-ins → AU (re-scan if needed); Batida is in the FX browser under AUi |

**Updating:** run the same line again, then quit and reopen the host. (Hosts keep the old
plugin loaded until they restart.)


## What to try

The [README](README.md) explains the pages, gestures and MIDI notes. Notes are given by number
as well as name, because hosts name octaves differently (note 60 is C3 in Logic and Live, C4 in
Reaper).

1. **First run:** open Batida in a new project. It starts with the neutral kit, empty patterns
   and modulators, and the library in `~/Music/Oddfield/Batida/`.
2. **KIT:** play notes 36–43 (C1–G1 in Logic) and move the XY pad. Does the chain do what the
   pad says?
3. **SOUND:** edit a sound. Try FM, Sample (drop a WAV on a slot) and Layer, the operators, and
   **VARY** (hold a tile to hear it, then keep one).
4. **SEQ:** load the *Breakbeat* pattern from LIB, or draw one. Hold note 60 (C3 in Logic) to
   play pattern 1, and try Latch, Transport mode with the host playing, lanes and XY locks.
5. **MOD:** draw a shape, then right-click a value → Modulate with Mod 1.
6. **LIB:** click through sounds and kits while a pattern plays, then press UNDO once. Save a
   sound and a kit, reopen the project, and check everything came back.
7. **Zoom and host:** CFG → 125% / 150%; automation; saving and reopening the project.

New in 0.8:

8. **Choke:** SOUND → Choke. The default kit's hats are in group 1: play the open hat, then the
   closed hat.
9. **Kit Vary:** SOUND → VARY → KIT, pick a direction, hold a tile while a pattern plays, KEEP
   one, then UNDO.
10. **Pattern Vary:** SEQ → VARY on a pattern with hits. Hold each tile (it plays even with the
    sequencer stopped); KEEP one; UNDO.
11. **Breaks:** drag a drum loop from Finder onto the SEQ grid. The kick, snare, hat and
    percussion slots take its hits; play the pattern at another tempo.
12. **Resample:** right-click a slot → Resample here → the pattern (a loop) or a sound. Then play
    that slot.
13. **MIDI out:** drag **MIDI ↗** onto a MIDI or instrument track in your host and play it back
    through Batida. If the drag doesn't work in your host, click MIDI ↗ and drag the file from
    Finder. The XY lane comes along as CC 16 and CC 17.
14. **XY REC:** KIT → XY REC, then hold the pad still while a pattern plays: each step it passes
    takes the pad's position.

New in 0.8.4: the publisher is now called **Oddfield**. Batida is listed under Oddfield in your
host, so projects saved with an earlier build need Batida added again. Your library and settings
move to the Oddfield folders by themselves the first time you open Batida.

New in 0.8.3:

15. **Stack:** SOUND → Stack 2–4 on the bass. Raise Detune and Spread and listen for a wide
    reese; then set Interval to Minor 7 on a short sound and play a few notes in Chromatic mode.
    Check it still sounds right in mono.

New in 0.9.0 (files and scripting, see the README):

16. **Files:** save a kit, open it in a text editor, change a value (a `flt_cutoff`, say) and
    save. The kit's name strip shows a lime ↻; click it to hear the change. A new file saved into
    the User folder shows up in LIB within a few seconds.
17. **The batida tool:** in Terminal, `batida render` a factory set to a WAV and `batida analyze`
    it; `batida validate` a file after misspelling a setting in it.

Everything you notice is useful: sound, workflow, UI, wording, crashes, CPU, and how it behaves
in your host.

## Known limits

- AU only, macOS only (VST3 comes later).
- The factory library: about 290 sounds, 40 kits, 58 patterns and 19 sets, still under review.
  The factory kits' hats aren't in a choke group yet; that comes with the next factory build.
- Resampling a long pattern or loading a long loop as a break holds the window for a moment
  while it renders.
- Dragging MIDI ↗ out of the plugin window may not work in hosts that run plugins in a separate
  process (Logic can); clicking it and dragging the file from Finder always works.
- Extreme drive (full clip or fold) can alias a little.
- The sidechain input has been checked the way a host does, not yet in every host. Each host
  routes a sidechain its own way (in Logic, the Side Chain menu at the top of the plugin window).

## Reporting

Open an issue in this repository (**Issues → New issue**) and pick **Bug** or **Feedback**.
Please include:
- the version (click **BATIDA/** in the plugin's top-left corner);
- your Mac, macOS, and host with its version;
- what you did, what you expected, and what happened;
- for a crash, the report from *Console → Crash Reports* (or `~/Library/Logs/DiagnosticReports`).

## Uninstall

```bash
curl -fsSL https://raw.githubusercontent.com/ikeaang-hue/Oddfield-Batida/main/install.sh | bash -s -- --uninstall
```

The library (`~/Music/Oddfield/Batida/`) holds your saved sounds, kits, resampled samples
and exported MIDI; delete it only if you don't need them. Settings are in
`~/Library/Application Support/Oddfield/Batida/`.

## Licence

Batida is free software under the GNU AGPL v3 ([LICENSE.md](LICENSE.md)); this repository is its
source.
