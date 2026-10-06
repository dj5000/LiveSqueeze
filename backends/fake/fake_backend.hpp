#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "lsq/audio_backend.hpp"

namespace lsq {

struct FakeConfig {
    double captureRate = 48000.0;
    double playbackRate = 48000.0;
    ChannelMap captureMap = ChannelMap::standard(Layout::Stereo);
    std::uint32_t capturePeriod = 480;
    std::uint32_t playbackPeriod = 480;
    double captureClockPpm = 0.0;  // > 0: the capture device's clock runs fast
    double playbackClockPpm = 0.0; // > 0: the playback device's clock runs fast
    double jitterMs = 0.0;         // each callback is delayed by up to this much, at random
    std::uint64_t seed = 1;
    bool failOpen = false;                // open() fails, to exercise error handling
    bool captureMonitorsPlayback = false; // the capture device is "Monitor of Fake speakers"
    bool playbackLooksVirtual = false;    // the playback device is listed as a virtual cable
    // Lifecycle hooks for tests: called when a backend is opened successfully and when it is
    // destroyed (enumeration-only instances never call onOpen).
    std::function<void(class FakeBackend*)> onOpen;
    std::function<void(class FakeBackend*)> onDestroy;

    // Fills `out` (frames * channels interleaved) for the capture callback. `startFrame` is the
    // index of the first frame since start. Default: a 1 kHz sine at -20 dBFS on every channel.
    std::function<void(float* out, std::uint32_t frames, std::uint64_t startFrame, int channels)>
        generator;
    // Receives every playback buffer after the engine has filled it (stereo interleaved).
    std::function<void(const float* stereo, std::uint32_t frames)> sink;
};

// A deterministic stand-in for real audio devices. Two modes:
//   * advance(): virtual time. The test drives the clock and callbacks run on the calling thread,
//     in order, with configurable clock drift, jitter, stalls and injected device events.
//   * start(): a thread runs the same simulation against the wall clock, for the supervisor and
//     the GUI tests.
class FakeBackend : public IAudioBackend {
public:
    explicit FakeBackend(FakeConfig cfg = {});
    ~FakeBackend() override;

    const char* name() const override { return "fake"; }
    BackendCaps caps() const override { return {}; }
    std::vector<DeviceInfo> enumerate(Dir dir) override;
    Status open(const OpenRequest& request, const AudioCallbacks& callbacks, EventCallback onEvent,
                void* eventUser) override;
    Status start() override;
    void stop() override;
    void close() override;
    NegotiatedInfo info() const override;

    // Virtual-time driver (call from one thread, not while start()'s thread runs).
    void advance(double seconds);
    // Suspend capture / playback callbacks for this long (virtual seconds from now).
    void stallCapture(double seconds);
    void stallPlayback(double seconds);
    // Delivers a device event to the callback registered in open().
    void injectEvent(DeviceEventKind kind);

    double now() const { return now_; }
    std::uint64_t captureFrames() const { return captureFrames_.load(); }
    std::uint64_t playbackFrames() const { return playbackFrames_.load(); }

private:
    // One simulated device: callbacks are due at `nominal` (a perfectly regular schedule, which
    // is what drifts relative to the other device) and happen at `actual` = nominal + lateness.
    struct Stream {
        double nominal = 0.0;
        double actual = 0.0;
        double step = 0.0; // seconds between callbacks, including the clock error
    };

    void runUntil(double target);
    double lateness();
    void reschedule(Stream& s);
    void runCaptureCallback();
    void runPlaybackCallback();

    FakeConfig cfg_;
    AudioCallbacks callbacks_{};
    EventCallback onEvent_ = nullptr;
    void* eventUser_ = nullptr;
    bool opened_ = false;

    double now_ = 0.0;
    Stream cap_;
    Stream play_;
    std::uint64_t rng_ = 1;
    std::vector<float> captureBuf_;
    std::vector<float> playbackBuf_;
    std::atomic<std::uint64_t> captureFrames_{0};
    std::atomic<std::uint64_t> playbackFrames_{0};

    std::thread thread_;
    std::atomic<bool> threadRun_{false};
    std::mutex mutex_; // serializes advance() between the thread and injectEvent()/stall calls
};

} // namespace lsq
