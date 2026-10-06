# Hardware test checklist

LiveSqueeze's processing, drift handling, channel mapping and recovery logic are tested automatically, and on Linux
the whole PipeWire path is tested end to end. What no automated test here could do is run on **real sound hardware,
real Windows and macOS audio stacks, real virtual cables and a real desktop**. This checklist is how to close that gap.
It takes about 15 minutes. Please paste the output of step 1 into any bug report.

## 1. Self-test report

```sh
lsq-run --list-devices
lsq-run --selftest --json
```

`--selftest` runs for three seconds against your default devices (or add `--input` / `--output`) and reports the
formats the devices agreed to, callback sizes, and any underruns or overruns.

Expected: `"pass": true`, `underruns` 0 or 1, `overruns` 0.

On Linux with PipeWire the input is the virtual sink LiveSqueeze creates, and `capture_callbacks` stays 0 until an
application plays into it. That is normal.

## 2. Does it sound right?

Play a film with a 5.1 soundtrack (any player) through LiveSqueeze with the "Movie / Night" preset:

```sh
lsq-run --input "<your virtual cable>" --output "<your speakers>" --preset movie-night
```

- [ ] Dialogue is clear and at a comfortable level.
- [ ] Whispered or distant speech is easier to hear than without LiveSqueeze.
- [ ] Explosions and music peaks are noticeably tamed, with no clipping or crackle.
- [ ] No clicks, pops or dropouts over ten minutes. (The status line shows `underruns`; it should stay at 0 or 1.)
- [ ] Switching to `--preset limiter-only` makes quiet speech quiet again, so you can hear the difference.
- [ ] Audio and picture stay in sync. If not, note how many milliseconds off, and what your player's audio-delay
      setting needed.

If something sounds pumping or unnatural, the settings are in the parameter table (`lsq-cli params`) and
[TUNING.md](TUNING.md).

## 3. Channel mapping

With a 5.1 test file that announces each speaker ("front left", "center", ...) played through LiveSqueeze with
`--preset limiter-only`:

- [ ] Front left and right come out on the left and right.
- [ ] Center (dialogue) is heard in both ears, centered.
- [ ] Rear/surround left and right come out on the left and right, quieter than the front.
- [ ] The subwoofer channel is silent by default (turn it on with `--set lfe_enabled=true`).

On macOS with the 16-channel BlackHole, the OS cannot say which channels carry 5.1, so add `--input-layout 5.1`.

## 4. Robustness

- [ ] Unplug and replug your headphones or speakers while playing. LiveSqueeze should recover within a few seconds
      (`lsq-run` prints `recovering`, then `running`).
- [ ] Change the sample rate of your output device in the system sound settings (for example 48 kHz to 44.1 kHz) while
      running. It should restart cleanly.
- [ ] Stop LiveSqueeze while a video is playing. Check that you can still get sound by switching the output back.
- [ ] Leave it running for an hour on a long film. No growing delay, no dropouts. If there are dropouts, run
      `lsq-run --trace trace.csv` instead and attach the file: it records every audio callback.

## 5. Per platform

**Windows**
- [ ] VB-Cable (or another virtual cable) is installed, and its playback volume is at 100%.
- [ ] In *Sound settings*, the cable's format is set to 5.1 or 7.1 surround (Advanced > Default format), otherwise
      applications will send it stereo.
- [ ] Your player or Windows output is set to the cable; LiveSqueeze's output is your real speakers.
- [ ] LiveSqueeze refuses to use the cable as its own output (it should say so).

**macOS**
- [ ] BlackHole 16ch is installed. Set the system output (or the player's output) to it.
- [ ] macOS asks permission to use the microphone the first time, because capturing from any input device does.
      Allow it.
- [ ] BlackHole has no volume control; LiveSqueeze's output volume setting is the volume.

**Linux**
- [ ] The LiveSqueeze output appears in the sound settings of your desktop environment.
- [ ] Setting it as the default output sends all applications through it.
- [ ] After stopping LiveSqueeze, applications return to your speakers.

## 6. Accessibility (when the tray app is available)

- [ ] Every control can be reached and changed with the keyboard alone.
- [ ] Your screen reader announces each control with a sensible name and its current value.
- [ ] The window is readable at 200% scaling and in high-contrast mode.
- [ ] The tray icon can be reached by keyboard and shows whether processing is on.
