#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "lsq/async_resampler.hpp"
#include "lsq/channel_map.hpp"
#include "lsq/downmix.hpp"
#include "lsq/drift_controller.hpp"
#include "lsq/dynamics_chain.hpp"
#include "lsq/frame_ring.hpp"
#include "lsq/meters.hpp"
#include "lsq/param_store.hpp"
#include "lsq/status.hpp"

namespace lsq {

// Trades delay for robustness against irregular device callbacks.
enum class LatencyMode { Low, Balanced, Safe };

struct EngineConfig {
    double captureRate = 48000.0;
    double playbackRate = 48000.0;
    ChannelMap captureMap = ChannelMap::standard(Layout::Stereo);
    std::uint32_t captureBlock = 480;  // typical capture callback size (frames), a hint
    std::uint32_t playbackBlock = 480; // typical playback callback size (frames), a hint
    LatencyMode latency = LatencyMode::Balanced;
    std::size_t maxPlaybackBlock = 8192; // larger callbacks are split
};

// A snapshot of the engine's counters. Safe to take from any thread.
struct EngineStats {
    std::uint64_t captureCallbacks = 0;
    std::uint64_t playbackCallbacks = 0;
    std::uint64_t underruns = 0;     // playback ran out of captured audio
    std::uint64_t overruns = 0;      // capture side had to drop frames (ring full)
    std::uint64_t overfillSkips = 0; // too much audio queued; the excess was discarded
    std::uint64_t primes = 0;        // times playback (re)started after waiting for data
    std::uint64_t adaptations =
        0;               // times the queue was enlarged for larger-than-advertised callbacks
    double fillMs = 0.0; // audio queued between capture and playback
    double targetFillMs = 0.0;
    double trimPpm = 0.0;   // current clock-drift correction
    double latencyMs = 0.0; // delay added by LiveSqueeze itself (excluding device buffers)
    bool running = false;   // false while waiting for the queue to fill
};

// The realtime heart: audio from the capture callback goes through a ring, a downmix, a
// drift-compensating resampler and the dynamics chain, and out of the playback callback.
//
// onCapture() and onPlayback() are called from the two device threads and never allocate, lock
// or block. configure() allocates and must only be called while neither is running.
class Engine {
public:
    explicit Engine(ParamStore* params = nullptr) noexcept : store_(params) {}

    Status configure(const EngineConfig& cfg);

    // Source of monotonic time in seconds, used to timestamp capture callbacks. The default is
    // the steady clock. Tests that simulate devices in virtual time supply their own. Call before
    // the devices start.
    using ClockFn = double (*)(void* user);
    void setClock(ClockFn fn, void* user) noexcept {
        clockFn_ = fn;
        clockUser_ = user;
    }

    void onCapture(const float* interleaved, std::uint32_t frames) noexcept;
    void onPlayback(float* stereo, std::uint32_t frames) noexcept;

    EngineStats stats() const noexcept;
    bool popMeter(MeterFrame& out) noexcept { return chain_.popMeter(out); }

    // Frames of delay the engine intends to hold, in capture-rate frames. It grows if the devices
    // turn out to use larger callbacks than they advertised.
    std::size_t targetFillFrames() const noexcept {
        return targetFill_.load(std::memory_order_relaxed);
    }
    const EngineConfig& config() const noexcept { return cfg_; }

private:
    // Queue size needed for the given callback sizes (playback frames, capture frames).
    std::size_t targetFor(std::size_t playbackBlock, std::size_t captureBlock) const noexcept;
    void playbackChunk(float* out, std::size_t frames) noexcept;
    // Reads input, downmixes and resamples `count` output frames. Returns how many were produced.
    std::size_t produce(float* out, std::size_t count, double ratio) noexcept;

    double now() const noexcept {
        if (clockFn_ != nullptr) {
            return clockFn_(clockUser_);
        }
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    ParamStore* store_ = nullptr;
    ClockFn clockFn_ = nullptr;
    void* clockUser_ = nullptr;
    Params params_;
    EngineConfig cfg_;
    bool configured_ = false;

    FrameRing ring_;
    Downmixer downmix_;
    AsyncResampler resampler_;
    DriftController drift_;
    DynamicsChain chain_;

    std::vector<float> inN_;      // N-channel input read from the ring
    std::vector<float> stereoIn_; // after downmix

    double nominalRatio_ = 1.0; // captureRate / playbackRate
    std::atomic<std::size_t> targetFill_{0};
    std::size_t overfill_ = 0;
    std::size_t maxTarget_ = 0;         // growth limit: what the ring can sensibly hold
    std::size_t playbackBlockSeen_ = 0; // playback thread only
    std::atomic<std::uint32_t> captureBlockSeen_{0};
    int fadeInFrames_ = 0;
    int fadeOutFrames_ = 0;
    int fadeInLeft_ = 0;
    bool running_ = false; // playback thread only

    std::atomic<std::uint64_t> captureCallbacks_{0};
    std::atomic<std::uint64_t> playbackCallbacks_{0};
    std::atomic<std::uint64_t> underruns_{0};
    std::atomic<std::uint64_t> overruns_{0};
    std::atomic<std::uint64_t> overfillSkips_{0};
    std::atomic<std::uint64_t> primes_{0};
    std::atomic<std::uint64_t> adaptations_{0};
    std::atomic<double> lastCaptureTime_{0.0};
    static_assert(std::atomic<double>::is_always_lock_free);
    std::atomic<double> fillFrames_{0.0};
    std::atomic<double> meanFillFrames_{0.0};
    std::atomic<double> trimPpm_{0.0};
    std::atomic<bool> runningFlag_{false};
};

} // namespace lsq
