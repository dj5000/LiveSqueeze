#include <atomic>
#include <thread>
#include <vector>

#include "doctest.h"
#include "lsq/frame_ring.hpp"

using namespace lsq;

TEST_CASE("FrameRing writes, reads and wraps around") {
    FrameRing ring;
    ring.configure(8, 2);
    CHECK(ring.available() == 0);
    CHECK(ring.capacity() == 8);

    std::vector<float> a(2 * 6);
    for (std::size_t i = 0; i < a.size(); ++i) {
        a[i] = static_cast<float>(i);
    }
    CHECK(ring.write(a.data(), 6) == 6);
    CHECK(ring.available() == 6);

    std::vector<float> out(2 * 4);
    CHECK(ring.read(out.data(), 4) == 4);
    for (std::size_t i = 0; i < 8; ++i) {
        CHECK(out[i] == static_cast<float>(i));
    }

    // Now 2 frames are queued at the end of the buffer; writing 6 more wraps around.
    std::vector<float> b(2 * 6);
    for (std::size_t i = 0; i < b.size(); ++i) {
        b[i] = 100.0f + static_cast<float>(i);
    }
    CHECK(ring.write(b.data(), 6) == 6);
    CHECK(ring.available() == 8);

    std::vector<float> all(2 * 8);
    CHECK(ring.read(all.data(), 8) == 8);
    CHECK(all[0] == 8.0f);
    CHECK(all[3] == 11.0f);
    CHECK(all[4] == 100.0f);
    CHECK(all[15] == 111.0f);
    CHECK(ring.available() == 0);
}

TEST_CASE("FrameRing accepts only what fits and reads only what is there") {
    FrameRing ring;
    ring.configure(4, 1);
    const float in[6] = {1, 2, 3, 4, 5, 6};
    CHECK(ring.write(in, 6) == 4); // the last two are dropped
    float out[6] = {};
    CHECK(ring.read(out, 6) == 4);
    CHECK(out[3] == 4.0f);
    CHECK(ring.read(out, 1) == 0);
}

TEST_CASE("FrameRing skip discards the oldest frames") {
    FrameRing ring;
    ring.configure(8, 1);
    const float in[5] = {1, 2, 3, 4, 5};
    ring.write(in, 5);
    CHECK(ring.skip(3) == 3);
    float out[2] = {};
    CHECK(ring.read(out, 2) == 2);
    CHECK(out[0] == 4.0f);
    CHECK(ring.skip(10) == 0);
}

TEST_CASE("FrameRing keeps order across threads") {
    FrameRing ring;
    ring.configure(1000, 2);
    constexpr std::uint32_t kFrames = 400000;
    std::thread producer([&] {
        std::uint32_t next = 0;
        std::vector<float> chunk(2 * 64);
        while (next < kFrames) {
            const std::uint32_t n = std::min<std::uint32_t>(64, kFrames - next);
            for (std::uint32_t i = 0; i < n; ++i) {
                chunk[2 * i] = static_cast<float>(next + i);
                chunk[2 * i + 1] = -static_cast<float>(next + i);
            }
            std::size_t done = 0;
            while (done < n) {
                done += ring.write(chunk.data() + 2 * done, n - done);
            }
            next += n;
        }
    });
    std::uint32_t expect = 0;
    bool ordered = true;
    std::vector<float> buf(2 * 100);
    while (expect < kFrames) {
        const std::size_t n = ring.read(buf.data(), 100);
        for (std::size_t i = 0; i < n; ++i) {
            ordered = ordered && buf[2 * i] == static_cast<float>(expect) &&
                      buf[2 * i + 1] == -static_cast<float>(expect);
            ++expect;
        }
    }
    producer.join();
    CHECK(ordered);
}
