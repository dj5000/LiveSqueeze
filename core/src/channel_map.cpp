#include "lsq/channel_map.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace lsq {
namespace {

ChannelMap make(std::initializer_list<Pos> list) {
    ChannelMap m;
    for (Pos p : list) {
        if (m.n < kMaxChannels) {
            m.pos[m.n++] = p;
        }
    }
    return m;
}

// WAVE_FORMAT_EXTENSIBLE speaker bits, in channel order.
constexpr Pos kWaveBits[] = {
    Pos::FL,      Pos::FR,      Pos::FC,      Pos::LFE,     Pos::BL,      Pos::BR,      Pos::FLC,
    Pos::FRC,     Pos::BC,      Pos::SL,      Pos::SR,      Pos::Unknown, // top center
    Pos::Unknown, Pos::Unknown, Pos::Unknown, Pos::Unknown, Pos::Unknown, Pos::Unknown,
};

} // namespace

const char* posName(Pos p) noexcept {
    switch (p) {
    case Pos::FL:
        return "FL";
    case Pos::FR:
        return "FR";
    case Pos::FC:
        return "FC";
    case Pos::LFE:
        return "LFE";
    case Pos::BL:
        return "BL";
    case Pos::BR:
        return "BR";
    case Pos::SL:
        return "SL";
    case Pos::SR:
        return "SR";
    case Pos::FLC:
        return "FLC";
    case Pos::FRC:
        return "FRC";
    case Pos::BC:
        return "BC";
    case Pos::Unknown:
        return "?";
    }
    return "?";
}

ChannelMap ChannelMap::standard(Layout layout) {
    switch (layout) {
    case Layout::Mono:
        return make({Pos::FC});
    case Layout::Stereo:
        return make({Pos::FL, Pos::FR});
    case Layout::Surround21:
        return make({Pos::FL, Pos::FR, Pos::LFE});
    case Layout::Quad:
        return make({Pos::FL, Pos::FR, Pos::BL, Pos::BR});
    case Layout::Surround51:
        return make({Pos::FL, Pos::FR, Pos::FC, Pos::LFE, Pos::BL, Pos::BR});
    case Layout::Surround71:
        return make({Pos::FL, Pos::FR, Pos::FC, Pos::LFE, Pos::BL, Pos::BR, Pos::SL, Pos::SR});
    }
    return make({Pos::FL, Pos::FR});
}

ChannelMap ChannelMap::fromCount(int channels) {
    switch (channels) {
    case 1:
        return standard(Layout::Mono);
    case 2:
        return standard(Layout::Stereo);
    case 3:
        return standard(Layout::Surround21);
    case 4:
        return standard(Layout::Quad);
    case 5:
        return make({Pos::FL, Pos::FR, Pos::FC, Pos::BL, Pos::BR});
    case 6:
        return standard(Layout::Surround51);
    case 7:
        return make({Pos::FL, Pos::FR, Pos::FC, Pos::LFE, Pos::BC, Pos::SL, Pos::SR});
    case 8:
        return standard(Layout::Surround71);
    default:
        break;
    }
    ChannelMap m;
    m.n = static_cast<std::uint8_t>(std::clamp(channels, 0, kMaxChannels));
    for (int i = 0; i < m.n; ++i) {
        m.pos[i] = i == 0 ? Pos::FL : (i == 1 ? Pos::FR : Pos::Unknown);
    }
    return m;
}

ChannelMap ChannelMap::fromWaveMask(std::uint32_t mask, int channels) {
    ChannelMap m;
    int bits = 0;
    for (int b = 0; b < 18; ++b) {
        if ((mask >> b) & 1u) {
            if (m.n < kMaxChannels) {
                m.pos[m.n++] = kWaveBits[b];
            }
            ++bits;
        }
    }
    if (mask == 0 || bits != channels) {
        return fromCount(channels);
    }
    return m;
}

bool ChannelMap::parse(std::string_view name, ChannelMap& out) {
    std::string s(name);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s == "mono" || s == "1.0") {
        out = standard(Layout::Mono);
    } else if (s == "stereo" || s == "2.0") {
        out = standard(Layout::Stereo);
    } else if (s == "2.1") {
        out = standard(Layout::Surround21);
    } else if (s == "quad" || s == "4.0") {
        out = standard(Layout::Quad);
    } else if (s == "5.1") {
        out = standard(Layout::Surround51);
    } else if (s == "7.1") {
        out = standard(Layout::Surround71);
    } else {
        return false;
    }
    return true;
}

bool ChannelMap::has(Pos p) const noexcept {
    for (int i = 0; i < n; ++i) {
        if (pos[i] == p) {
            return true;
        }
    }
    return false;
}

std::string ChannelMap::toString() const {
    std::string s;
    for (int i = 0; i < n; ++i) {
        if (i > 0) {
            s += ' ';
        }
        s += posName(pos[i]);
    }
    return s;
}

} // namespace lsq
