# Architecture

LiveSqueeze is split so that everything which decides how the audio *sounds* is plain portable C++ with no operating
system dependencies, and everything which touches an audio device is a thin, replaceable backend.

```
core/      DSP only: downmix, compressor, limiter, parameters, presets. No OS calls, no allocation in process().
engine/    OS-free glue: ring buffer, drift-compensating resampler, Engine, device Supervisor.
backends/  IAudioBackend implementations: fake (tests), miniaudio (Windows/macOS/Linux-Pulse), PipeWire (Linux).
cli/       lsq-cli: offline WAV in / WAV out using the same core, plus signal generator and analyzer.
apps/run/  lsq-run: headless live runner.
app/       Qt 6 tray application.
```

## Signal flow

```
app audio → [virtual device] → capture callback → SPSC ring (N channels, capture rate)
playback callback (the only thread that runs DSP):
    ring → Downmixer (N → 2) → AsyncResampler (rate from DriftController)
         → DynamicsChain [ Compressor → Limiter → bypass crossfade → output trim → safety clamp ]
         → real output device
```

## Threads

| Thread | Does | Must not |
|---|---|---|
| GUI | widgets, publishes parameters, polls meters | touch backend internals |
| Worker / supervisor | open, start, stop, enumerate, recover from device changes | run in a real-time context |
| Capture callback | copy samples into the ring | anything else |
| Playback callback | all DSP, drift control, meter push | allocate, lock, do I/O, log, throw |

Parameters reach the audio thread through a wait-free triple buffer (`ParamStore`). Meters travel back through a
single-producer/single-consumer ring. No mutex is shared with the audio thread.

## DSP

### Downmix

ITU-R BS.775 Lo/Ro coefficients: `L = FL + 0.707·FC + 0.707·SL (+ LFE)`. The center and surround levels are adjustable;
the LFE channel is off by default. Coefficient changes are ramped per sample, so they never click.

### Compressor (one curve, one smoother)

The detector is power-linked across both channels (so the stereo image does not shift), measured over about 10 ms and
calibrated so that a full-scale sine reads 0 dB. The static curve, in dB of detector level `L`, is:

- **Above the downward threshold `Td`:** soft-knee downward compression at ratio `Rd`.
- **Between the upward threshold `Tu` and `Td`:** no change. This is where normal dialogue should sit.
- **Below `Tu`:** a boost of `min(Bmax, (Tu − L)·(1 − 1/Ru))`, tapered to zero over the 10 dB above the noise floor, so
  room tone and hiss are not inflated.

The target gain is smoothed with the attack time when it falls and the release time when it rises. A loud event is
turned down quickly; quiet passages recover slowly.

### Limiter

A look-ahead limiter. For every sample it computes the gain needed to stay under the ceiling, takes a sliding-window
minimum over the look-ahead time, applies a release envelope that never rises above that minimum, and smooths the result
with a box filter of the same length. The audio is delayed so the gain is always in place before the peak arrives, which
guarantees the ceiling by construction. Peaks are measured on an 8× oversampled copy of the signal (a 32-tap polyphase
interpolator, detection only), so the ceiling is a true-peak ceiling in dBTP.

An interpolator of finite length cannot be exact for broadband material, whose reconstructed peaks depend on many distant
samples. Measured against an independent 16× long-filter oracle in the test suite, the limited output stays within
about 0.05 dB of the ceiling for tones, square waves and sample-pair worst cases, within about 0.4 dB for heavily
overdriven low-passed noise, and within about 0.7 dB for full-band white noise driven 12 dB over full scale. The sample
values themselves never exceed the ceiling. The default ceiling of −1 dBTP leaves room for all of this.

### Real-time rules

No allocation, locks, I/O or exceptions in `process()`. Denormals are flushed to zero on the audio thread. Filter and
envelope state is kept in `double`. Output is independent of the block size.

## Latency

LiveSqueeze adds three things to the path: the queue between the capture and playback devices, the resampler's look-ahead
(0.5 ms) and the limiter's look-ahead (5.3 ms). The queue is sized from the device callback sizes and a safety margin
chosen by the latency mode:

| Mode | Safety margin | Added by LiveSqueeze (480-frame callbacks at 48 kHz) |
|---|---|---|
| Low | 2 ms | about 23 ms |
| Balanced (default) | 5 ms | about 26 ms |
| Safe | 12 ms | about 33 ms |

These are the engine's own delay, measured in simulation. The devices add their own buffering on top (typically
10–40 ms), which only real hardware can show; `lsq-run --selftest` reports the periods each device agreed to.

## Clock drift

Capture and playback devices run on separate clocks on Windows and macOS. The engine keeps the queue near a target fill
level with a small PI controller that nudges the resampling ratio by at most ±2000 ppm (0.2%, about 3.5 cents), so no
audio is dropped or repeated in normal operation.

Two details matter in practice, and both were found by simulation:

- **The target must cover the whole worst case.** Audio arrives in capture-sized chunks and leaves in playback-sized
  chunks, so the fill seen at each playback callback swings by about a block, plus callback jitter. The queue therefore
  holds one playback block, the resampler's look-ahead, half a capture block and the jitter margin.
- **The fill must be measured "fluidly".** Because the two clocks differ slightly, the order of the capture and playback
  callbacks slowly changes (once per ~100 s at 100 ppm), and the raw ring fill jumps by a whole capture block each time.
  A controller fed that signal hunts. The engine adds the audio the capture device has accumulated since its last
  callback (time since the last capture callback × rate), which removes the jump. With this, a 100 ppm offset gives a
  trim within noise of 100 ppm and a fill within 0.1 ms of target.

Underruns fade out over about 2 ms, then wait for the queue to refill and fade back in over 10 ms. A queue that
overfills (a long stall on the playback side) is trimmed back to target.

## Device management

The supervisor owns the backend and the engine on a worker thread. It opens the devices, and restarts them with
exponential backoff (250 ms up to 5 s) when a device disappears, changes format or when playback callbacks stop for 1.5 s.
It lists devices every 2 s to notice plugging and unplugging. It refuses feedback loops: the output may not be a virtual
cable, and the input may not be a "Monitor of" the output.

## Milestones

| Milestone | Content |
|---|---|
| M0 | Build system, CI, scaffolding |
| M1 | DSP core, offline CLI, unit tests |
| M2 | Engine, supervisor, backend interface, miniaudio backend, headless runner |
| M3 | Linux PipeWire virtual-sink backend |
| M4 | Qt tray GUI |
| M5 | Windows backend and documentation |
| M6 | macOS backend and documentation |
| M7 | Packaging and release (later) |
