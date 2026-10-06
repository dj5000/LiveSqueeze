#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace lsq {

// Single-producer / single-consumer ring of interleaved float frames with a runtime capacity.
// The capture callback writes, the playback callback reads. Wait-free, no allocation after
// configure().
class FrameRing {
public:
    // Allocates and empties the ring. Not thread safe: call while neither side is running.
    void configure(std::size_t capacityFrames, int channels) {
        channels_ = static_cast<std::size_t>(channels);
        capacity_ = capacityFrames;
        buf_.assign(capacityFrames * channels_, 0.0f);
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    // Producer. Copies as many frames as fit and returns that number.
    std::size_t write(const float* in, std::size_t frames) noexcept {
        const std::uint64_t h = head_.load(std::memory_order_relaxed);
        const std::uint64_t t = tail_.load(std::memory_order_acquire);
        const std::size_t space = capacity_ - static_cast<std::size_t>(h - t);
        const std::size_t n = std::min(frames, space);
        const std::size_t idx = static_cast<std::size_t>(h % capacity_);
        const std::size_t first = std::min(n, capacity_ - idx);
        std::memcpy(&buf_[idx * channels_], in, first * channels_ * sizeof(float));
        if (n > first) {
            std::memcpy(&buf_[0], in + first * channels_, (n - first) * channels_ * sizeof(float));
        }
        head_.store(h + n, std::memory_order_release);
        return n;
    }

    // Consumer. Copies up to `frames` frames out and returns that number.
    std::size_t read(float* out, std::size_t frames) noexcept {
        const std::uint64_t t = tail_.load(std::memory_order_relaxed);
        const std::uint64_t h = head_.load(std::memory_order_acquire);
        const std::size_t n = std::min(frames, static_cast<std::size_t>(h - t));
        const std::size_t idx = static_cast<std::size_t>(t % capacity_);
        const std::size_t first = std::min(n, capacity_ - idx);
        std::memcpy(out, &buf_[idx * channels_], first * channels_ * sizeof(float));
        if (n > first) {
            std::memcpy(out + first * channels_, &buf_[0], (n - first) * channels_ * sizeof(float));
        }
        tail_.store(t + n, std::memory_order_release);
        return n;
    }

    // Consumer. Discards up to `frames` frames and returns that number.
    std::size_t skip(std::size_t frames) noexcept {
        const std::uint64_t t = tail_.load(std::memory_order_relaxed);
        const std::uint64_t h = head_.load(std::memory_order_acquire);
        const std::size_t n = std::min(frames, static_cast<std::size_t>(h - t));
        tail_.store(t + n, std::memory_order_release);
        return n;
    }

    // Frames currently readable. Exact for the consumer, a lower bound for the producer.
    std::size_t available() const noexcept {
        return static_cast<std::size_t>(head_.load(std::memory_order_acquire) -
                                        tail_.load(std::memory_order_acquire));
    }

    std::size_t capacity() const noexcept { return capacity_; }
    int channels() const noexcept { return static_cast<int>(channels_); }

private:
    std::vector<float> buf_;
    std::size_t capacity_ = 1;
    std::size_t channels_ = 1;
    alignas(64) std::atomic<std::uint64_t> head_{0};
    alignas(64) std::atomic<std::uint64_t> tail_{0};
};

} // namespace lsq
