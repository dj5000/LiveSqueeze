// Cross-thread tests for the lock-free primitives. They are most useful under ThreadSanitizer.

#include <atomic>
#include <cstdint>
#include <thread>

#include "doctest.h"
#include "lsq/param_store.hpp"
#include "lsq/spsc_ring.hpp"

using namespace lsq;

namespace {
// A value whose fields must always be seen together; a torn read breaks the invariant.
struct Pair {
    std::uint64_t a = 0;
    std::uint64_t b = 0; // always a + 1000
    std::uint64_t pad[6] = {};
};
} // namespace

TEST_CASE("TripleBuffer never delivers a torn value") {
    TripleBuffer<Pair> tb;
    std::atomic<bool> done{false};
    constexpr std::uint64_t kCount = 200000;

    std::thread writer([&] {
        for (std::uint64_t i = 1; i <= kCount; ++i) {
            Pair p;
            p.a = i;
            p.b = i + 1000;
            tb.publish(p);
        }
        done = true;
    });

    std::uint64_t last = 0;
    std::uint64_t reads = 0;
    bool consistent = true;
    bool monotonic = true;
    while (!done.load() || reads == 0) {
        Pair p;
        if (tb.fetch(p)) {
            ++reads;
            consistent = consistent && (p.b == p.a + 1000);
            monotonic = monotonic && (p.a > last);
            last = p.a;
        }
    }
    writer.join();
    Pair p;
    if (tb.fetch(p)) {
        last = p.a;
    }
    CHECK(consistent);
    CHECK(monotonic);
    CHECK(last == kCount); // the final value is always delivered
}

TEST_CASE("SpscRing preserves order across threads") {
    SpscRing<std::uint32_t, 64> ring;
    constexpr std::uint32_t kCount = 300000;

    std::thread producer([&] {
        for (std::uint32_t i = 0; i < kCount;) {
            if (ring.push(i)) {
                ++i;
            }
        }
    });

    std::uint32_t expect = 0;
    bool inOrder = true;
    while (expect < kCount) {
        std::uint32_t v;
        if (ring.pop(v)) {
            inOrder = inOrder && (v == expect);
            ++expect;
        }
    }
    producer.join();
    CHECK(inOrder);
    std::uint32_t v;
    CHECK_FALSE(ring.pop(v));
}

TEST_CASE("SpscRing reports full and empty") {
    SpscRing<int, 4> ring;
    int v = 0;
    CHECK_FALSE(ring.pop(v));
    for (int i = 0; i < 4; ++i) {
        CHECK(ring.push(i));
    }
    CHECK_FALSE(ring.push(99)); // full: dropped, not overwritten
    CHECK(ring.sizeApprox() == 4);
    for (int i = 0; i < 4; ++i) {
        CHECK(ring.pop(v));
        CHECK(v == i);
    }
    // Wrap around a few times.
    for (int round = 0; round < 10; ++round) {
        CHECK(ring.push(round));
        CHECK(ring.pop(v));
        CHECK(v == round);
    }
}
