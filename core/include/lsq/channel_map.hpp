#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lsq {

inline constexpr int kMaxChannels = 16;

// Speaker positions. Back and side surrounds are treated alike by the downmix.
enum class Pos : std::uint8_t { FL, FR, FC, LFE, BL, BR, SL, SR, FLC, FRC, BC, Unknown };

enum class Layout { Mono, Stereo, Surround21, Quad, Surround51, Surround71 };

// Which speaker each interleaved channel of a stream carries.
struct ChannelMap {
    std::uint8_t n = 0;
    Pos pos[kMaxChannels] = {};

    // Standard (WAVE / SMPTE / ALSA / PipeWire) channel order for a layout.
    static ChannelMap standard(Layout layout);

    // Best guess for a plain channel count: 1 mono, 2 stereo, 4 quad, 6 5.1, 8 7.1, ...
    static ChannelMap fromCount(int channels);

    // From a WAVE_FORMAT_EXTENSIBLE channel mask (also used by WASAPI). Falls back to fromCount()
    // when the mask is empty or does not match the channel count.
    static ChannelMap fromWaveMask(std::uint32_t mask, int channels);

    // Parses "mono", "stereo", "2.1", "quad", "5.1", "7.1" (case-insensitive).
    static bool parse(std::string_view name, ChannelMap& out);

    bool has(Pos p) const noexcept;
    std::string toString() const; // e.g. "FL FR FC LFE BL BR"
};

const char* posName(Pos p) noexcept;

} // namespace lsq
