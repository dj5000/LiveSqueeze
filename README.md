# LiveSqueeze

A realtime audio compressor and limiter that works as a virtual audio device.

It takes the audio your system is playing, converts multichannel sound (for example 5.1 or 7.1) to stereo, applies
dynamic range compression so loud passages are turned down and quiet ones (whispers, distant dialogue) are lifted and
easier to hear, then limits the peaks and plays the result on your real speakers or headphones.

> **Status:** early development. The design and milestones are described in
> [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). This README is updated as each milestone lands.

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
| Linux | Native PipeWire virtual sink created by LiveSqueeze | planned (M3) |
| Windows | An existing virtual cable such as VB-Cable, captured through WASAPI | planned (M5) |
| macOS | [BlackHole](https://github.com/ExistentialAudio/BlackHole) (16ch for 5.1/7.1), captured through CoreAudio | planned (M6) |

A custom virtual device of our own on Windows and macOS is a later goal. It needs driver signing on Windows and an
AudioServerPlugin on macOS, so version 1 uses existing virtual cables there.

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
