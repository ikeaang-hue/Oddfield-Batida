# Testing Batida (trusted testers)

Thanks for trying Batida before anyone else. This is a **pre-release build (0.6.1)** for a small
group of developers: you build it yourself from this repository. Please don't share the plugin
or the repository yet.

## What you need

- A Mac with macOS 12 or later (Apple Silicon or Intel: you build for your own Mac).
- Xcode with its command line tools, and CMake (`brew install cmake`).
- Logic Pro, or another host that loads Audio Units.

## Build and install

```bash
git clone --recurse-submodules https://github.com/ikeaang-hue/NegativeSpace-Batida.git
cd NegativeSpace-Batida
cmake -B build -G Xcode
cmake --build build --config Release
killall -9 AudioComponentRegistrar
```

The build installs `~/Library/Audio/Plug-Ins/Components/Batida.component`. The first build takes
a few minutes. Quit and relaunch Logic, then insert *AU Instruments → Negative Space → Batida*.

**Updating:** `git pull && git submodule update --init`, build again, run the `killall` line,
then relaunch Logic.

Optional checks after building (they should all end in PASS):

```bash
build/BatidaTests_artefacts/Release/BatidaTests
build/BatidaSnapshot_artefacts/Release/BatidaSnapshot build/snapshots
auval -v aumu Btda Ngsp
```

## What to try

The [README](README.md) explains the pages, gestures and MIDI notes. Useful things to try:

1. **First run:** open Batida in a new project. It starts with the neutral kit, empty patterns
   and modulators, and the library in `~/Music/Negative Space/Batida/`.
2. **KIT:** play C1–G1 and move the XY pad. Does the chain do what the pad says?
3. **SOUND:** edit a sound. Try FM, Sample (drop a WAV on a slot) and Layer, the operators, and
   **VARY** (hold a tile to hear it, then keep one).
4. **SEQ:** load the *Breakbeat* pattern from LIB, or draw one. Hold C3 to play pattern 1, and
   try Latch, Transport mode with Logic playing, lanes and XY locks.
5. **MOD:** draw a shape, then right-click a value → Modulate with Mod 1.
6. **LIB:** click through sounds and kits while a pattern plays, then press UNDO once. Save a
   sound and a kit, reopen the project, and check everything came back.
7. **Zoom and host:** CFG → 125% / 150%; automation; saving and reopening the Logic project.

Everything you notice is useful: sound, workflow, UI, wording, crashes, CPU.

## Known limits

- AU only, macOS only (VST3 comes later).
- The factory library is small for now: the neutral kit, its 8 sounds, one pattern and one set.
  More sounds and kits come in the next phase.
- Extreme drive (full clip or fold) can alias a little.
- The sidechain input has been checked the way a host does, not yet in every host.

## Reporting

Open an issue in this repository (**Issues → New issue**) and pick **Bug** or **Feedback**.
Please include:
- the version (click **BATIDA/** in the plugin's top-left corner);
- your Mac, macOS and Logic versions;
- what you did, what you expected, and what happened;
- for a crash, the report from *Console → Crash Reports* (or `~/Library/Logs/DiagnosticReports`).

## Uninstall

```bash
rm -rf ~/Library/Audio/Plug-Ins/Components/Batida.component
killall -9 AudioComponentRegistrar
```

The library (`~/Music/Negative Space/Batida/`) holds your saved sounds and kits; delete it only
if you don't need them. Settings are in `~/Library/Application Support/Negative Space/Batida/`.

## Licence

Batida is free software under the GNU AGPL v3; this repository is its source.
