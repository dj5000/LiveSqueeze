# Tuning guide

The presets are starting points chosen from typical film levels, not from listening tests with you in the room. This is
how to adjust them. All settings can be given to the command line tools with `--set key=value`; `lsq-cli params` lists
them with their ranges.

## What the compressor does

Levels below are measured by the detector, which is calibrated so that a full-scale sine reads 0 dB. In typical film and
streaming mixes, dialogue sits around −30 to −20, whispers around −45 to −40, and action scenes around −15 to −8.

```
 gain (dB)
   +9 |_________                     boost quiet sounds, up to "maximum boost"
      |         \___
    0 |             \_______________                 leave dialogue alone
      |                             \___
  −10 |                                 \____        turn loud sounds down
      +-----------------------------------------> detector level (dB)
       −60   −45   −34        −24       −10
        noise    boost      normal    compress
        floor    below      dialogue  above
```

- **Threshold** (`threshold_db`): above this level sounds are turned down by the **ratio**.
- **Boost below** (`up_threshold_db`): below this level sounds are turned up, by (1 − 1/**boost ratio**) dB for every dB
  below it, never more than **maximum boost**.
- **Noise floor** (`noise_floor_db`): the boost fades out to nothing over the 10 dB above this level, so room tone and
  hiss are not lifted.
- Between the two thresholds nothing changes. Put normal dialogue there.

Attack (`attack_ms`) is how fast the gain *falls* when something loud happens; release (`release_ms`) is how slowly it
*rises* again when it is quiet. Slow release avoids audible pumping.

## Common adjustments

| I want... | Try |
|---|---|
| Whispers louder | raise `max_boost_db` (e.g. 12), or raise `up_threshold_db` toward the threshold |
| Dialogue louder against music and effects | `--preset dialogue-boost`, or raise `center_gain_db` |
| Less hiss lifted | raise `noise_floor_db` (e.g. −55) |
| Loud scenes quieter | lower `threshold_db`, or raise `ratio` |
| Less "pumping" (volume breathing) | raise `release_ms` (e.g. 700), lower `max_boost_db` |
| Quiet speech to recover faster after a loud scene | lower `release_ms` (e.g. 250). Too low and the background noise breathes. |
| Overall louder | `makeup_db` (the limiter still holds the peak at the ceiling) |
| Subwoofer in the mix | `--set lfe_enabled=true` |
| Rear speakers quieter | `surround_gain_db` below 0 |

## A/B comparison without a sound card

```sh
lsq-cli gen --kind movie --layout 5.1 --seconds 40 --out film.wav
lsq-cli process film.wav a.wav --preset movie-night
lsq-cli process film.wav b.wav --preset movie-night --set max_boost_db=12 --set release_ms=250
lsq-cli analyze b.wav     # peaks, true peak, loudness spread
lsq-cli curve --preset movie-night   # the static curve as CSV
```

Use your own recordings with `lsq-cli process in.wav out.wav`; multichannel WAV files are downmixed the same way as live
audio.

## Latency

`--latency low|balanced|safe` trades delay for margin against irregular device timing. Balanced adds about 26 ms,
low about 23 ms, safe about 33 ms (plus the devices' own buffers). If you hear dropouts, use safe; if lip-sync is off,
use your player's audio-delay setting first.
