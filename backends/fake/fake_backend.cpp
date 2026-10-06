#include "fake_backend.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace lsq {
namespace {
constexpr double kPi = 3.14159265358979323846;
}

FakeBackend::FakeBackend(FakeConfig cfg) : cfg_(std::move(cfg)) {
    if (!cfg_.generator) {
        const double rate = cfg_.captureRate;
        cfg_.generator = [rate](float* out, std::uint32_t frames, std::uint64_t start, int ch) {
            for (std::uint32_t i = 0; i < frames; ++i) {
                const auto v = static_cast<float>(
                    0.1 * std::sin(2.0 * kPi * 1000.0 * static_cast<double>(start + i) / rate));
                for (int c = 0; c < ch; ++c) {
                    out[static_cast<std::size_t>(i) * static_cast<std::size_t>(ch) +
                        static_cast<std::size_t>(c)] = v;
                }
            }
        };
    }
}

FakeBackend::~FakeBackend() {
    stop();
    if (cfg_.onDestroy) {
        cfg_.onDestroy(this);
    }
}

std::vector<DeviceInfo> FakeBackend::enumerate(Dir dir) {
    DeviceInfo d;
    d.id = dir == Dir::Capture ? "fake-capture" : "fake-playback";
    d.name = dir == Dir::Capture ? (cfg_.captureMonitorsPlayback ? "Monitor of Fake speakers"
                                                                 : "Fake virtual cable")
                                 : "Fake speakers";
    d.channels = dir == Dir::Capture ? cfg_.captureMap.n : 2;
    d.isDefault = true;
    d.looksVirtual = dir == Dir::Capture ? true : cfg_.playbackLooksVirtual;
    return {d};
}

Status FakeBackend::open(const OpenRequest&, const AudioCallbacks& callbacks, EventCallback onEvent,
                         void* eventUser) {
    if (cfg_.failOpen) {
        return Status::error("fake backend: open failed (as configured)");
    }
    callbacks_ = callbacks;
    onEvent_ = onEvent;
    eventUser_ = eventUser;
    rng_ = cfg_.seed * 0x9E3779B97F4A7C15ull + 1;
    captureBuf_.assign(static_cast<std::size_t>(cfg_.capturePeriod) *
                           static_cast<std::size_t>(cfg_.captureMap.n),
                       0.0f);
    playbackBuf_.assign(static_cast<std::size_t>(cfg_.playbackPeriod) * 2, 0.0f);
    now_ = 0.0;
    cap_ = Stream{};
    play_ = Stream{};
    cap_.step = cfg_.capturePeriod / (cfg_.captureRate * (1.0 + cfg_.captureClockPpm * 1e-6));
    play_.step = cfg_.playbackPeriod / (cfg_.playbackRate * (1.0 + cfg_.playbackClockPpm * 1e-6));
    cap_.actual = cap_.nominal + lateness();
    play_.actual = play_.nominal + lateness();
    captureFrames_ = playbackFrames_ = 0;
    opened_ = true;
    if (cfg_.onOpen) {
        cfg_.onOpen(this);
    }
    return Status::success();
}

Status FakeBackend::start() {
    std::lock_guard<std::mutex> lifecycle(lifecycleMu_);
    if (!opened_) {
        return Status::error("fake backend: not open");
    }
    if (thread_.joinable()) {
        return Status::success();
    }
    threadRun_.store(true);
    thread_ = std::thread([this] {
        using clock = std::chrono::steady_clock;
        const auto t0 = clock::now();
        while (threadRun_.load()) {
            const double wall = std::chrono::duration<double>(clock::now() - t0).count();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (wall > now_) {
                    runUntil(wall);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    return Status::success();
}

// Idempotent and synchronous: every caller returns only once the thread has been joined, so the
// object can be destroyed safely afterwards no matter which thread stopped it first.
void FakeBackend::stop() {
    std::lock_guard<std::mutex> lifecycle(lifecycleMu_);
    threadRun_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
}

void FakeBackend::close() {
    stop();
    opened_ = false;
}

NegotiatedInfo FakeBackend::info() const {
    NegotiatedInfo n;
    n.captureRate = cfg_.captureRate;
    n.playbackRate = cfg_.playbackRate;
    n.captureMap = cfg_.captureMap;
    n.capturePeriodFrames = cfg_.capturePeriod;
    n.playbackPeriodFrames = cfg_.playbackPeriod;
    n.sharedClock = cfg_.sharedCycle;
    n.captureName = "Fake virtual cable";
    n.playbackName = "Fake speakers";
    return n;
}

// Random delay in [0, jitterMs).
double FakeBackend::lateness() {
    if (cfg_.jitterMs <= 0.0) {
        return 0.0;
    }
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 7;
    rng_ ^= rng_ << 17;
    const double u = static_cast<double>(rng_ >> 11) / 9007199254740992.0;
    return u * cfg_.jitterMs * 1e-3;
}

// Delay between the capture and playback callbacks of one shared cycle.
double FakeBackend::intraCycleGap() {
    if (cfg_.intraCycleWanderMs <= 0.0) {
        return 0.0;
    }
    auto uniform = [this] {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 7;
        rng_ ^= rng_ << 17;
        return static_cast<double>(rng_ >> 11) / 9007199254740992.0;
    };
    if (now_ >= gapChangeAt_) {
        gapBase_ = uniform() * cfg_.intraCycleWanderMs * 1e-3;
        gapChangeAt_ = now_ + 1.0 + 2.0 * uniform(); // changes every 1 to 3 seconds
    }
    const double noise = (uniform() - 0.5) * 0.6e-3;
    return std::clamp(gapBase_ + noise, 0.0, cfg_.intraCycleWanderMs * 1e-3);
}

void FakeBackend::reschedule(Stream& s) {
    s.nominal += s.step;
    s.actual = std::max(s.nominal + lateness(), now_);
}

void FakeBackend::runCaptureCallback() {
    const std::uint32_t n = cfg_.capturePeriod;
    cfg_.generator(captureBuf_.data(), n, captureFrames_.load(), cfg_.captureMap.n);
    if (callbacks_.capture != nullptr) {
        callbacks_.capture(callbacks_.user, captureBuf_.data(), n);
    }
    captureFrames_ += n;
}

void FakeBackend::runPlaybackCallback() {
    const std::uint32_t n = cfg_.playbackPeriod;
    std::fill(playbackBuf_.begin(), playbackBuf_.end(), 0.0f);
    if (callbacks_.playback != nullptr) {
        callbacks_.playback(callbacks_.user, playbackBuf_.data(), n);
    }
    if (cfg_.sink) {
        cfg_.sink(playbackBuf_.data(), n);
    }
    playbackFrames_ += n;
}

void FakeBackend::runUntil(double target) {
    for (;;) {
        if (cfg_.sharedCycle) {
            // One clock, one cycle: capture first, then playback, back to back.
            if (cap_.actual > target) {
                break;
            }
            now_ = cap_.actual;
            if (cfg_.playbackFirstAfter >= 0.0 && cap_.actual >= cfg_.playbackFirstAfter) {
                runPlaybackCallback();
                now_ = cap_.actual + intraCycleGap();
                runCaptureCallback();
            } else {
                runCaptureCallback();
                now_ = cap_.actual + intraCycleGap();
                runPlaybackCallback();
            }
            now_ = cap_.actual;
            reschedule(cap_);
            play_.actual = cap_.actual;
            continue;
        }
        const bool capFirst = cap_.actual <= play_.actual;
        Stream& s = capFirst ? cap_ : play_;
        if (s.actual > target) {
            break;
        }
        now_ = s.actual;
        if (capFirst) {
            runCaptureCallback();
        } else {
            runPlaybackCallback();
        }
        reschedule(s);
    }
    now_ = target;
}

void FakeBackend::advance(double seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    runUntil(now_ + seconds);
}

// A stalled device delivers nothing for a while and does not catch up afterwards.
void FakeBackend::stallCapture(double seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    cap_.nominal = std::max(cap_.nominal, now_ + seconds);
    cap_.actual = cap_.nominal + lateness();
}

void FakeBackend::stallPlayback(double seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    play_.nominal = std::max(play_.nominal, now_ + seconds);
    play_.actual = play_.nominal + lateness();
}

void FakeBackend::injectEvent(DeviceEventKind kind) {
    if (onEvent_ != nullptr) {
        onEvent_(eventUser_, DeviceEvent{kind});
    }
}

} // namespace lsq
