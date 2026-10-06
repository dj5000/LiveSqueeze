# LiveSqueeze

A realtime audio compressor and limiter that works as a virtual audio device.

It takes the audio your system is playing, converts multichannel sound (for example 5.1 or 7.1) to stereo, applies
dynamic range compression so loud passages are turned down and quiet ones (whispers, distant dialogue) are lifted and
easier to hear, then limits the peaks and plays the result on your real speakers or headphones.

> **Status:** early development. The DSP, the realtime engine, a headless runner, the Linux (PipeWire) backend and the
> Qt tray app exist and are tested (without real sound hardware, see below); the Windows/macOS backends are being added. Design and milestones:
> [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). What has and has not been verified on real hardware:
> [docs/HARDWARE_TEST_CHECKLIST.md](docs/HARDWARE_TEST_CHECKLIST.md).

## How it works

```
apps / players ──▶ virtual audio device ──▶ LiveSqueeze ──▶ speakers / headphones
                    (5.1 / 7.1 / stereo)    downmix → compressor → limiter
```

- **Downmix:** ITU-R BS.775 style 5.1/7.1 → stereo, with an adjustable center (dialogue) boost.
- **Compressor:** one gain curve that turns loud sounds down, leaves normal dialogue alone, and lifts quiet sounds up to a
  capped amount. A noise floor stops room tone and hiss from being boosted.
- **Limiter:** look-ahead true-peak limiter, so the output never clips even after the boost.
- **Presets:** Movie / Night, Late night, Dialogue boost, Gentle leveler, Limiter only.

## Platforms

| Platform | Virtual device | Status |
|---|---|---|
| Linux | Native PipeWire virtual sink created by LiveSqueeze | working, tested end to end on a headless PipeWire |
| Windows | An existing virtual cable such as VB-Cable, captured through WASAPI | planned (M5) |
| macOS | [BlackHole](https://github.com/ExistentialAudio/BlackHole) (16ch for 5.1/7.1), captured through CoreAudio | planned (M6) |

A custom virtual device of our own on Windows and macOS is a later goal. It needs driver signing on Windows and an
AudioServerPlugin on macOS, so version 1 uses existing virtual cables there.

## The tray app

`livesqueeze` (built from `app/` when Qt 6 is installed) is the everyday way to use it: a window with a **strength**
slider and presets, level meters, a picture of the compression curve, an Advanced page with every setting, and a Devices
page; and a tray icon with the switches you need while watching a film (processing on/off, bypass, preset). It is built
to be usable with the keyboard alone, with large text and with a screen reader; see
[docs/ACCESSIBILITY.md](docs/ACCESSIBILITY.md) for what has and has not been checked.

## Trying it

The offline tool runs the same DSP on WAV files, so you can hear what the compressor does without any audio setup:

```sh
build/cli/lsq-cli gen --kind movie --layout 5.1 --seconds 40 --out film.wav   # a made-up film soundtrack
build/cli/lsq-cli process film.wav squeezed.wav --preset movie-night          # downmix + compress + limit
build/cli/lsq-cli analyze squeezed.wav
```

The headless live runner works with real devices. On Linux with PipeWire just run `lsq-run`, then choose the new
**LiveSqueeze** output in your player or sound settings. On Windows and macOS pick the virtual cable and your speakers
(`lsq-run --list-devices`, then `lsq-run --input "CABLE Output" --output "Speakers"`). `lsq-run --selftest --json`
prints a report that is useful in bug reports. See [docs/PLATFORMS.md](docs/PLATFORMS.md) and
[docs/TUNING.md](docs/TUNING.md).

## Building

Requires CMake 3.25+ and a C++20 compiler (GCC 12+, Clang 15+, MSVC 2022, AppleClang 15+).

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

On Debian/Ubuntu, `scripts/dev-setup-linux.sh` installs everything including the optional PipeWire and Qt pieces.
Presets for debug, ASan/UBSan and TSan builds are in `CMakePresets.json`.

Third-party headers are vendored in `third_party/` (see its README), so nothing is downloaded at build time.

## License

GPL-3.0, see [LICENSE](LICENSE).
