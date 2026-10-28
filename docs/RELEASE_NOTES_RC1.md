# LiveSqueeze 0.1 release candidate 1 (private testing)

This is a release candidate for **private user testing**, not a public release. It contains everything from the
initial development branch (DSP core, realtime engine, Linux PipeWire backend, WASAPI/CoreAudio through miniaudio, the
Qt tray app) plus the fixes below, found in a release-readiness review of the compressor/limiter.

## Fixes in this candidate

1. **Settings are kept when the audio restarts.** When the audio restarted (headphones unplugged and replugged, the
   output's sample rate changed, a device vanished, the watchdog restarted a stalled stream), the new engine started with
   the *default* settings instead of yours, and kept them until you touched a control. Your output volume, bypass,
   preset and every advanced setting were silently lost. With the output volume at -30 dB the sound came back 27 dB
   louder. The engine now starts from the settings last published.
2. **Lowering the limiter ceiling while audio plays no longer clips.** Audio already inside the limiter's 5 ms look-ahead
   was clamped flat to the new, lower ceiling: about 3 ms of squared-off peaks, heard as a click or crackle. This happened
   when switching from *Limiter only* (strength 0 %) to *Late night* or lowering *Ceiling* during loud audio. The new
   ceiling now takes over once that audio has played out (about 5 ms later, a smooth hand-over).
3. **A corrupt sample can no longer silence the output for good.** A single huge but finite sample (for example from an
   uninitialised or corrupt buffer) overflowed the compressor's level detector to infinity. Its gain became NaN, and the
   output stayed broken until the audio was restarted. NaN and infinity were already handled; now input is also limited
   to +60 dBFS, the detector works in double precision, and the final safety clamp turns NaN into silence.

The intended sound is unchanged. Presets, defaults, attack/release behaviour and the latency are the same. The level
detector now works in double precision, which changes output samples only at rounding level.

## What was tested in the cloud (Linux, no sound hardware)

- GCC 13 and Clang 18 builds with warnings as errors. All four test suites pass (core DSP, engine, backend, CLI
  round-trip), and so do the ASan + UBSan and TSan builds.
- New regression tests run on synthetic audio. Each fails on the code before the fixes and passes after:
  - `a restarted engine keeps the settings that were already published` (engine).
  - `lowering the ceiling while limiting does not square off the peaks` and `raising the ceiling takes effect at once
    and does not clip` (limiter).
  - `switching from Limiter only to Late night during loud audio does not clip` and `a garbage sample far beyond full
    scale cannot silence the output for good` (full chain).
- `scripts/xcheck_ffmpeg.sh`: ffmpeg's independent loudness-range and true-peak meters agree that the range is reduced
  and the true peak stays at the ceiling.
- `lsq-run --backend null --selftest`: the live engine with software-timed null devices and real threads, with no
  underruns and no overruns.
- On push, GitHub Actions also builds and unit-tests on Windows (MSVC) and macOS, and runs the PipeWire end-to-end
  test. Those runs use simulated or null devices.

**Not validated by any of this:** real Windows audio devices and drivers, VB-Cable, WASAPI timing and latency on real
hardware, Bluetooth headsets, long sessions on a real machine, and how it actually sounds. Those need the checklist
below.

## Build and run

Requirements: CMake 3.25+ and a C++20 compiler (Visual Studio 2022 on Windows). Qt 6 is optional (tray app).

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

**Windows testers don't need to build anything.** Each CI run attaches **LiveSqueeze-windows-app**, a folder that
runs as it is: the tray app (`livesqueeze.exe`) with its Qt and Visual C++ DLLs, the command-line tools and the
step-by-step guide [WINDOWS_TESTING.md](WINDOWS_TESTING.md) (in the download as `How to test.txt`). Find it in the
Actions tab → the run for this branch → Artifacts.

Windows, headless runner (after installing [VB-Cable](https://vb-audio.com/Cable/), see
[PLATFORMS.md](PLATFORMS.md#windows)):

```bat
lsq-run --list-devices
lsq-run --input "CABLE Output" --output "<your speakers>" --preset movie-night
lsq-run --selftest --json
```

Offline, with no audio devices: `lsq-cli gen --kind movie --layout 5.1 --seconds 40 --out film.wav`, then
`lsq-cli process film.wav squeezed.wav --preset movie-night`.

## Windows listening checklist (about 20 minutes)

The easiest way is the app: follow [WINDOWS_TESTING.md](WINDOWS_TESTING.md), which needs only the mouse. The same
checks with the command-line runner are below. The complete list is in
[HARDWARE_TEST_CHECKLIST.md](HARDWARE_TEST_CHECKLIST.md). These are the items most relevant to this candidate:

1. [ ] `lsq-run --selftest --json` reports `"pass": true`, at most 1 underrun and 0 overruns. Keep the output for bug
       reports.
2. [ ] Play a film with the cable as the output and `--preset movie-night`. Dialogue is clear and whispers are easier to
       hear. Explosions are tamed with no crackle. Ten minutes pass with no clicks or dropouts.
3. [ ] **Fix 1:** with `--set out_trim_db=-20` playing, unplug and replug the headphones (or change the speakers' sample
       rate in Sound settings). After `recovering` → `running` the volume is still the quieter one, not suddenly louder.
       In the tray app: set a low output volume and bypass, replug, and check both survive.
4. [ ] **Fix 2:** in the tray app, play loud music at strength 0 % (*Limiter only*), then pick *Late night*, or lower
       *Ceiling* on the Advanced page. There should be no click or crackle at the moment of the change.
5. [ ] Audio stays in sync with the picture. If it doesn't, note how many milliseconds of audio delay the player needed.
6. [ ] Leave a long film running for an hour: no growing delay, no dropouts. If there are dropouts, run with
       `--trace trace.csv` and keep the file.
7. [ ] If you use a screen reader or keyboard only, check that the tray app's main controls work. Screen readers have
       not been tested at all.

Fix 3 (a corrupt sample) cannot practically be triggered by hand. The synthetic tests cover it.

## Known limitations

- Windows and macOS use an existing virtual cable (VB-Cable / BlackHole). There is no LiveSqueeze-owned virtual device
  on those platforms yet.
- Nothing has been run on real Windows or macOS audio hardware yet (see [PLATFORMS.md](PLATFORMS.md)).
- No installer or signed binaries. Testers use the unzipped CI download, and Windows warns that it is unsigned.
