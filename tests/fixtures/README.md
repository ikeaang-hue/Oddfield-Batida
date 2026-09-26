# Old-project fixtures

`project-<version>.batida-state` is a project state (what a host saves) written by that
tagged release of Batida itself, so the current build is tested against real old
projects, not ones made up from the current code.

Each one was made by building the release's `BatidaSnapshot` target with
`WriteState.cpp.txt` (renamed to `.cpp`) in place of `tests/Snapshot.cpp`, then running
it with the output path. The state holds:

- sound 1 Level −7.5 dB, sound 8 Amp Decay 900 ms, the pad's Y at 0.6;
- sound 3 named "Crack";
- pattern 3: 24 steps, a hit on track 5, step 10;
- sound 4's sample at `/tmp/batida-fixture/old-tone.wav`, deleted afterwards, so it
  opens as missing with its path kept.

0.6.1 and 0.7.2 write byte-identical states; `project-0.7.2` stands for both. Add a
fixture from each release that changes the saved format.
