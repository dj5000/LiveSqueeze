#include "lsq/engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "lsq/rt_utils.hpp"

namespace lsq {
namespace {

double marginMs(LatencyMode m) {
    switch (m) {
    case LatencyMode::Low:
        return 2.0;
    case LatencyMode::Balanced:
        return 5.0;
    case LatencyMode::Safe:
        return 12.0;
    }
    return 5.0;
}

} // namespace

// Target queue size (capture frames), measured at the start of each playback callback. It has to
// cover what one playback block needs (its input plus the resampler's look-ahead), half a capture
// block (audio arrives in chunks, so the fill measured at callback time swings by about a block),
// and a margin for callback jitter.
std::size_t Engine::targetFor(std::size_t playbackBlock, std::size_t captureBlock) const noexcept {
    const double margin = marginMs(cfg_.latency) * 1e-3 * cfg_.captureRate;
    const double playbackInCaptureFrames = static_cast<double>(playbackBlock) * nominalRatio_;
    const double reach = std::ceil(AsyncResampler::kHalfTaps * std::max(1.0, nominalRatio_));
    return static_cast<std::size_t>(std::ceil(playbackInCaptureFrames + reach +
                                              0.5 * static_cast<double>(captureBlock) + margin));
}

Status Engine::configure(const EngineConfig& cfg) {
    if (cfg.captureRate < 8000.0 || cfg.captureRate > 768000.0 || cfg.playbackRate < 8000.0 ||
        cfg.playbackRate > 768000.0) {
        return Status::error("unsupported sample rate");
    }
    if (cfg.captureMap.n < 1 || cfg.captureMap.n > kMaxChannels) {
        return Status::error("unsupported number of capture channels");
    }
    cfg_ = cfg;
    cfg_.captureBlock = std::max<std::uint32_t>(cfg_.captureBlock, 1);
    cfg_.playbackBlock = std::max<std::uint32_t>(cfg_.playbackBlock, 1);
    cfg_.maxPlaybackBlock = std::max<std::size_t>(cfg_.maxPlaybackBlock, 64);

    nominalRatio_ = cfg_.captureRate / cfg_.playbackRate;
    const double reach = std::ceil(AsyncResampler::kHalfTaps * std::max(1.0, nominalRatio_));
    const std::size_t target = targetFor(cfg_.playbackBlock, cfg_.captureBlock);
    targetFill_.store(target, std::memory_order_relaxed);
    overfill_ = 3 * target + cfg_.captureBlock;

    // The ring is generous: it must survive a stall, and it leaves room for the queue to grow if
    // the devices turn out to use larger callbacks than they advertised.
    const std::size_t ringFrames =
        std::max(static_cast<std::size_t>(0.5 * cfg_.captureRate), 32 * target);
    ring_.configure(ringFrames, cfg_.captureMap.n);
    maxTarget_ = ringFrames / 6;

    // The largest input a single (split) playback chunk can need, with 1% ratio headroom.
    const auto maxIn = static_cast<std::size_t>(
        static_cast<double>(cfg_.maxPlaybackBlock) * nominalRatio_ * 1.01 + 2.0 * reach + 16.0);
    resampler_.prepare(nominalRatio_, maxIn);
    inN_.assign(maxIn * static_cast<std::size_t>(cfg_.captureMap.n), 0.0f);
    stereoIn_.assign(maxIn * 2, 0.0f);

    downmix_.prepare(cfg_.captureMap, cfg_.playbackRate);
    chain_.prepare(cfg_.playbackRate, 1024);
    // The drift controller works on the "fluid" fill, which includes audio the capture device has
    // already taken in but not yet delivered: on average that is half a capture block.
    drift_.configure(cfg_.captureRate,
                     static_cast<double>(target) + 0.5 * static_cast<double>(cfg_.captureBlock));

    fadeInFrames_ = std::max(1, static_cast<int>(0.010 * cfg_.playbackRate));
    fadeOutFrames_ = std::max(1, static_cast<int>(0.002 * cfg_.playbackRate));
    fadeInLeft_ = 0;
    running_ = false;
    runningFlag_.store(false);
    playbackBlockSeen_ = cfg_.playbackBlock;
    captureBlockSeen_.store(cfg_.captureBlock);
    params_ = Params{};
    captureCallbacks_ = playbackCallbacks_ = underruns_ = overruns_ = overfillSkips_ = primes_ = 0;
    adaptations_ = 0;
    lastCaptureTime_ = 0.0;
    fillFrames_ = 0.0;
    meanFillFrames_ = 0.0;
    trimPpm_ = 0.0;
    configured_ = true;
    return Status::success();
}

void Engine::onCapture(const float* in, std::uint32_t frames) noexcept {
    if (!configured_) {
        return;
    }
    captureCallbacks_.fetch_add(1, std::memory_order_relaxed);
    lastCaptureTime_.store(now(), std::memory_order_relaxed);
    if (frames > captureBlockSeen_.load(std::memory_order_relaxed)) {
        captureBlockSeen_.store(frames, std::memory_order_relaxed);
    }
    const std::size_t written = ring_.write(in, frames);
    if (written < frames) {
        overruns_.fetch_add(1, std::memory_order_relaxed);
    }
}

void Engine::onPlayback(float* out, std::uint32_t frames) noexcept {
    if (!configured_) {
        std::memset(out, 0, sizeof(float) * 2 * frames);
        return;
    }
    ScopedFtz ftz;
    playbackCallbacks_.fetch_add(1, std::memory_order_relaxed);
    if (store_ != nullptr) {
        store_->fetch(params_);
    }
    std::size_t left = frames;
    while (left > 0) {
        const std::size_t n = std::min(left, cfg_.maxPlaybackBlock);
        playbackChunk(out, n);
        out += 2 * n;
        left -= n;
    }
}

std::size_t Engine::produce(float* out, std::size_t count, double ratio) noexcept {
    const std::size_t need = resampler_.inputFramesNeeded(count, ratio);
    const std::size_t got = ring_.read(inN_.data(), need);
    downmix_.process(inN_.data(), stereoIn_.data(), got, params_);
    return resampler_.process(stereoIn_.data(), got, out, count, ratio);
}

void Engine::playbackChunk(float* out, std::size_t frames) noexcept {
    const double dt = static_cast<double>(frames) / cfg_.playbackRate;
    std::size_t fill = ring_.available();
    fillFrames_.store(static_cast<double>(fill), std::memory_order_relaxed);

    // Devices do not always use the callback sizes they advertise (a PipeWire graph may run a
    // larger quantum than requested, for instance). If larger blocks than planned for show up,
    // enlarge the queue and start over once, rather than underrunning forever.
    const std::size_t captureSeen = captureBlockSeen_.load(std::memory_order_relaxed);
    playbackBlockSeen_ = std::max(playbackBlockSeen_, frames);
    const std::size_t wanted = std::min(targetFor(playbackBlockSeen_, captureSeen), maxTarget_);
    if (wanted > targetFill_.load(std::memory_order_relaxed)) {
        targetFill_.store(wanted, std::memory_order_relaxed);
        overfill_ = 3 * wanted + captureSeen;
        drift_.configure(cfg_.captureRate,
                         static_cast<double>(wanted) + 0.5 * static_cast<double>(captureSeen));
        adaptations_.fetch_add(1, std::memory_order_relaxed);
        if (running_) {
            // Re-prime: let the queue fill to the new target before continuing.
            running_ = false;
            runningFlag_.store(false, std::memory_order_relaxed);
        }
    }
    const std::size_t targetFill = targetFill_.load(std::memory_order_relaxed);

    // Fluid fill: what is queued in the ring plus what the capture device has accumulated since it
    // last called us. Unlike the ring fill, it does not jump by a whole capture block whenever the
    // two devices' callbacks change order, which they do slowly because their clocks differ. It is
    // what the drift controller regulates, so start-up and trimming use it as well: starting
    // exactly on target avoids a half-block error that would take seconds to correct, during
    // which the queue is too thin and underruns.
    const double sinceCapture = now() - lastCaptureTime_.load(std::memory_order_relaxed);
    const double hidden =
        std::clamp(sinceCapture * cfg_.captureRate, 0.0, 2.0 * static_cast<double>(captureSeen));
    const double fluidTarget =
        static_cast<double>(targetFill) + 0.5 * static_cast<double>(captureSeen);
    auto skipExcess = [&] {
        const double excess = static_cast<double>(fill) + hidden - fluidTarget;
        if (excess >= 1.0) {
            ring_.skip(static_cast<std::size_t>(excess));
            fill = ring_.available();
        }
    };

    if (!running_) {
        if (static_cast<double>(fill) + hidden < fluidTarget) {
            std::memset(out, 0, sizeof(float) * 2 * frames);
            chain_.process(out, frames, params_); // keep the chain's tail flowing
            return;
        }
        // Enough audio has arrived: start (or restart) playback with a short fade-in, trimming the
        // queue to exactly on target while the fade-in hides the jump.
        skipExcess();
        resampler_.reset();
        drift_.reset();
        running_ = true;
        runningFlag_.store(true, std::memory_order_relaxed);
        fadeInLeft_ = fadeInFrames_;
        primes_.fetch_add(1, std::memory_order_relaxed);
    }

    // Too much queued (a long stall on the playback side): drop the excess to get back on target.
    if (fill > overfill_) {
        skipExcess();
        overfillSkips_.fetch_add(1, std::memory_order_relaxed);
        fadeInLeft_ = std::max(fadeInLeft_, std::max(1, fadeInFrames_ / 3));
    }

    const double trim = drift_.update(static_cast<double>(fill) + hidden, dt);
    trimPpm_.store(trim, std::memory_order_relaxed);
    meanFillFrames_.store(drift_.smoothedFill() - 0.5 * static_cast<double>(captureSeen),
                          std::memory_order_relaxed);
    const double ratio = nominalRatio_ * (1.0 + trim * 1e-6);

    std::size_t produced;
    if (ring_.available() >= resampler_.inputFramesNeeded(frames, ratio)) {
        produced = produce(out, frames, ratio);
    } else {
        // Underrun: play what is available, fade it out, then wait for the queue to refill.
        const std::size_t possible =
            std::min(frames, resampler_.outputFramesPossible(ring_.available(), ratio));
        produced = possible > 0 ? produce(out, possible, ratio) : 0;
        const std::size_t fade =
            std::min<std::size_t>(produced, static_cast<std::size_t>(fadeOutFrames_));
        for (std::size_t i = 0; i < fade; ++i) {
            const float g = 1.0f - static_cast<float>(i + 1) / static_cast<float>(fade);
            out[2 * (produced - fade + i)] *= g;
            out[2 * (produced - fade + i) + 1] *= g;
        }
        running_ = false;
        runningFlag_.store(false, std::memory_order_relaxed);
        underruns_.fetch_add(1, std::memory_order_relaxed);
    }
    if (produced < frames) {
        std::memset(out + 2 * produced, 0, sizeof(float) * 2 * (frames - produced));
    }

    // Fade in after (re)starting so the first samples do not click.
    for (std::size_t i = 0; fadeInLeft_ > 0 && i < produced; ++i, --fadeInLeft_) {
        const float g = 1.0f - static_cast<float>(fadeInLeft_) / static_cast<float>(fadeInFrames_);
        out[2 * i] *= g;
        out[2 * i + 1] *= g;
    }

    chain_.process(out, frames, params_);
}

EngineStats Engine::stats() const noexcept {
    EngineStats s;
    s.captureCallbacks = captureCallbacks_.load(std::memory_order_relaxed);
    s.playbackCallbacks = playbackCallbacks_.load(std::memory_order_relaxed);
    s.underruns = underruns_.load(std::memory_order_relaxed);
    s.overruns = overruns_.load(std::memory_order_relaxed);
    s.overfillSkips = overfillSkips_.load(std::memory_order_relaxed);
    s.primes = primes_.load(std::memory_order_relaxed);
    s.adaptations = adaptations_.load(std::memory_order_relaxed);
    s.running = runningFlag_.load(std::memory_order_relaxed);
    if (configured_) {
        const double target = static_cast<double>(targetFill_.load(std::memory_order_relaxed));
        const double fill = s.running ? meanFillFrames_.load(std::memory_order_relaxed)
                                      : fillFrames_.load(std::memory_order_relaxed);
        s.fillMs = fill / cfg_.captureRate * 1000.0;
        s.targetFillMs = target / cfg_.captureRate * 1000.0;
        s.trimPpm = trimPpm_.load(std::memory_order_relaxed);
        s.latencyMs = (target + resampler_.latencyFrames()) / cfg_.captureRate * 1000.0 +
                      static_cast<double>(chain_.latencyFrames()) / cfg_.playbackRate * 1000.0;
    }
    return s;
}

} // namespace lsq
