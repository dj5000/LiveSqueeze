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
guarantees the ceiling by construction. Peaks are measured on an 8× oversampled copy of the signal (detection only), so
the ceiling is a true-peak ceiling in dBTP. Like any oversampling meter this slightly underestimates tones very close to
half the sample rate; the default ceiling of −1 dBTP leaves room for that.

### Real-time rules

No allocation, locks, I/O or exceptions in `process()`. Denormals are flushed to zero on the audio thread. Filter and
envelope state is kept in `double`. Output is independent of the block size.

## Latency

Added latency is roughly the capture period + a small safety margin + the playback period + the limiter look-ahead.
The engine reports the figure it computed. Three modes will trade latency against robustness (Low, Balanced, Safe).

## Clock drift

Capture and playback devices run on separate clocks on Windows and macOS. The engine keeps the ring buffer near a target
fill level with a small PI controller that nudges the resampling ratio by at most ±500 ppm, so no audio is dropped or
repeated in normal operation.

## Milestones

| Milestone | Content |
|---|---|
| M0 | Build system, CI, scaffolding |
| M1 | DSP core, offline CLI, unit tests |
| M2 | Engine, backend interface, miniaudio backend, headless runner |
| M3 | Linux PipeWire virtual-sink backend |
| M4 | Qt tray GUI |
| M5 | Windows backend and documentation |
| M6 | macOS backend and documentation |
| M7 | Packaging and release (later) |
