#pragma once

#include <string>

#include "wav_io.hpp"

namespace lsqcli {

// Synthetic test signals. `kind` is one of: sine, noise, tones, movie.
//   sine   1 kHz tone on every channel at -12 dBFS
//   noise  white noise at -20 dBFS on every channel
//   tones  a different tone (frequency) per channel, for checking channel mapping and downmix
//   movie  a made-up film soundtrack: speech-like dialogue and whispers in the center channel,
//          explosions on every channel, ambience in the surrounds
// Returns false and fills `error` for an unknown kind.
bool generateSignal(const std::string& kind, const lsq::ChannelMap& map, std::uint32_t sampleRate,
                    double seconds, Audio& out, std::string& error);

} // namespace lsqcli
