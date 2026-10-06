#include "doctest.h"
#include "lsq/channel_map.hpp"

using namespace lsq;

TEST_CASE("standard layouts have the expected channel order") {
    CHECK(ChannelMap::standard(Layout::Stereo).toString() == "FL FR");
    CHECK(ChannelMap::standard(Layout::Surround51).toString() == "FL FR FC LFE BL BR");
    CHECK(ChannelMap::standard(Layout::Surround71).toString() == "FL FR FC LFE BL BR SL SR");
    CHECK(ChannelMap::standard(Layout::Mono).n == 1);
    CHECK(ChannelMap::standard(Layout::Quad).n == 4);
}

TEST_CASE("fromCount guesses sensible layouts") {
    CHECK(ChannelMap::fromCount(1).has(Pos::FC));
    CHECK(ChannelMap::fromCount(2).toString() == "FL FR");
    CHECK(ChannelMap::fromCount(6).toString() == "FL FR FC LFE BL BR");
    CHECK(ChannelMap::fromCount(8).n == 8);
    const ChannelMap odd = ChannelMap::fromCount(10);
    CHECK(odd.n == 10);
    CHECK(odd.pos[0] == Pos::FL);
    CHECK(odd.pos[1] == Pos::FR);
    CHECK(odd.pos[2] == Pos::Unknown);
    CHECK(ChannelMap::fromCount(40).n == kMaxChannels);
}

TEST_CASE("fromWaveMask reads WAVE_FORMAT_EXTENSIBLE masks") {
    CHECK(ChannelMap::fromWaveMask(0x3, 2).toString() == "FL FR");
    CHECK(ChannelMap::fromWaveMask(0x3F, 6).toString() == "FL FR FC LFE BL BR");
    // 5.1 with side surrounds (mask 0x60F)
    CHECK(ChannelMap::fromWaveMask(0x60F, 6).toString() == "FL FR FC LFE SL SR");
    CHECK(ChannelMap::fromWaveMask(0x63F, 8).toString() == "FL FR FC LFE BL BR SL SR");
    // Empty mask or a mask that disagrees with the channel count falls back to the count.
    CHECK(ChannelMap::fromWaveMask(0, 6).toString() == "FL FR FC LFE BL BR");
    CHECK(ChannelMap::fromWaveMask(0x3, 6).toString() == "FL FR FC LFE BL BR");
}

TEST_CASE("parse accepts layout names case-insensitively") {
    ChannelMap m;
    CHECK(ChannelMap::parse("5.1", m));
    CHECK(m.n == 6);
    CHECK(ChannelMap::parse("Stereo", m));
    CHECK(m.n == 2);
    CHECK(ChannelMap::parse("QUAD", m));
    CHECK(m.n == 4);
    CHECK_FALSE(ChannelMap::parse("9.1.4", m));
}
