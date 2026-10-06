#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lsq/channel_map.hpp"

namespace lsqcli {

// A whole WAV file in memory: interleaved 32-bit float samples.
struct Audio {
    std::uint32_t sampleRate = 48000;
    lsq::ChannelMap map;
    std::vector<float> samples; // interleaved, map.n channels

    std::size_t frames() const { return map.n == 0 ? 0 : samples.size() / map.n; }
};

// Reads PCM or float WAV (including WAVE_FORMAT_EXTENSIBLE multichannel). Returns false and fills
// `error` on failure.
bool readWav(const std::string& path, Audio& out, std::string& error);

// Writes 32-bit float WAV. Returns false and fills `error` on failure.
bool writeWav(const std::string& path, const Audio& audio, std::string& error);

} // namespace lsqcli
