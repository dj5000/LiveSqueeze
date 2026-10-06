#include "lsq/supervisor.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace lsq {
namespace {

using Clock = std::chrono::steady_clock;

std::string joinNames(const NegotiatedInfo& i) {
    return i.captureName + " \xE2\x86\x92 " + i.playbackName; // "→"
}

const DeviceInfo* findById(const std::vector<DeviceInfo>& v, const std::string& id) {
    for (const DeviceInfo& d : v) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

} // namespace

const char* stateName(SupervisorState s) noexcept {
    switch (s) {
    case SupervisorState::Idle:
        return "idle";
    case SupervisorState::Starting:
        return "starting";
    case SupervisorState::Running:
        return "running";
    case SupervisorState::Recovering:
        return "recovering";
    case SupervisorState::Failed:
        return "failed";
    }
    return "?";
}

Supervisor::Supervisor(BackendFactory factory, ParamStore* params)
    : factory_(std::move(factory)), params_(params) {}

Supervisor::~Supervisor() {
    stop();
}

void Supervisor::setStateCallback(std::function<void(SupervisorState, const std::string&)> cb) {
    std::lock_guard<std::mutex> lock(mu_);
    stateCb_ = std::move(cb);
}

void Supervisor::start(const SupervisorConfig& cfg) {
    std::lock_guard<std::mutex> lock(mu_);
    cfg_ = cfg;
    configChanged_ = true;
    if (!worker_.joinable()) {
        quit_ = false;
        worker_ = std::thread([this] { workerMain(); });
    }
    cv_.notify_all();
}

void Supervisor::stop() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!worker_.joinable()) {
            return;
        }
        quit_ = true;
        cv_.notify_all();
    }
    worker_.join();
    std::lock_guard<std::mutex> lock(mu_);
    worker_ = std::thread();
    state_ = SupervisorState::Idle;
    status_ = "Stopped";
}

SupervisorState Supervisor::state() const {
    std::lock_guard<std::mutex> lock(mu_);
    return state_;
}

std::string Supervisor::statusText() const {
    std::lock_guard<std::mutex> lock(mu_);
    return status_;
}

std::string Supervisor::lastError() const {
    std::lock_guard<std::mutex> lock(mu_);
    return error_;
}

EngineStats Supervisor::stats() const {
    std::lock_guard<std::mutex> lock(engineMu_);
    return engine_ ? engine_->stats() : EngineStats{};
}

NegotiatedInfo Supervisor::negotiated() const {
    std::lock_guard<std::mutex> lock(engineMu_);
    return info_;
}

bool Supervisor::popMeter(MeterFrame& out) {
    std::lock_guard<std::mutex> lock(engineMu_);
    return engine_ && engine_->popMeter(out);
}

std::vector<DeviceInfo> Supervisor::listDevices(Dir dir) {
    std::unique_ptr<IAudioBackend> b = factory_();
    return b ? b->enumerate(dir) : std::vector<DeviceInfo>{};
}

void Supervisor::setState(SupervisorState s, std::string status, std::string error) {
    std::function<void(SupervisorState, const std::string&)> cb;
    std::string text;
    {
        std::lock_guard<std::mutex> lock(mu_);
        state_ = s;
        status_ = std::move(status);
        error_ = std::move(error);
        cb = stateCb_;
        text = status_;
    }
    if (cb) {
        cb(s, text);
    }
}

void Supervisor::postEvent(const DeviceEvent& e) {
    std::lock_guard<std::mutex> lock(mu_);
    events_.push_back(e);
    cv_.notify_all();
}

void Supervisor::eventTrampoline(void* user, const DeviceEvent& e) {
    static_cast<Supervisor*>(user)->postEvent(e);
}

bool Supervisor::backendHasNativeHotplug() const {
    std::lock_guard<std::mutex> lock(engineMu_);
    return backend_ && backend_->caps().nativeHotplugEvents;
}

void Supervisor::writeTrace() const {
    if (tracePath_.empty() || !engine_ || engine_->traceSize() == 0) {
        return;
    }
    std::FILE* f = std::fopen(tracePath_.c_str(), "w");
    if (f == nullptr) {
        return;
    }
    std::fprintf(f, "time_s,kind,frames,fill,trim_ppm,peak\n");
    const TraceEvent* ev = engine_->traceData();
    const std::size_t n = engine_->traceSize();
    for (std::size_t i = 0; i < n; ++i) {
        std::fprintf(f, "%.6f,%s,%u,%u,%.1f,%.6f\n", ev[i].time - ev[0].time,
                     ev[i].kind == 0 ? "capture" : "playback", ev[i].frames, ev[i].fill,
                     static_cast<double>(ev[i].trimPpm), static_cast<double>(ev[i].peak));
    }
    std::fclose(f);
}

void Supervisor::teardown() {
    std::lock_guard<std::mutex> lock(engineMu_);
    if (backend_) {
        backend_->stop(); // returns only when no callback is running
        backend_->close();
        backend_.reset();
    }
    writeTrace();
    engine_.reset();
    info_ = NegotiatedInfo{};
}

// Chooses concrete devices and applies the loop guard.
Status Supervisor::resolveDevices(const SupervisorConfig& cfg, IAudioBackend& backend,
                                  OpenRequest& out) {
    out = cfg.request;
    const std::vector<DeviceInfo> playbacks = backend.enumerate(Dir::Playback);

    if (out.playbackId.empty()) {
        // Never default to a virtual device: it would feed our output back into our input.
        const DeviceInfo* pick = nullptr;
        for (const DeviceInfo& d : playbacks) {
            if (d.isDefault && !d.looksVirtual) {
                pick = &d;
                break;
            }
        }
        for (const DeviceInfo& d : playbacks) {
            if (pick == nullptr && !d.looksVirtual) {
                pick = &d;
            }
        }
        if (pick == nullptr) {
            return Status::error("No output device found (only virtual devices are available)");
        }
        out.playbackId = pick->id;
    } else if (const DeviceInfo* d = findById(playbacks, out.playbackId)) {
        if (d->looksVirtual) {
            return Status::error(
                "The output device '" + d->name +
                "' looks like a virtual cable. Choose your speakers or headphones.");
        }
    } else {
        return Status::error("The chosen output device is not available");
    }

    if (!out.createVirtualSink) {
        const std::vector<DeviceInfo> captures = backend.enumerate(Dir::Capture);
        if (out.captureId.empty()) {
            const DeviceInfo* pick = nullptr;
            for (const DeviceInfo& d : captures) {
                if (d.looksVirtual) {
                    pick = &d;
                    break;
                }
            }
            if (pick == nullptr) {
                return Status::error(
                    "No virtual audio cable found. Install one (see the setup guide) and choose it "
                    "as the input.");
            }
            out.captureId = pick->id;
        } else if (findById(captures, out.captureId) == nullptr) {
            return Status::error("The chosen input device is not available");
        }
        // Capturing the output itself would be a feedback loop: the same device on both sides, or
        // a PulseAudio/PipeWire "Monitor of <output>" source.
        const DeviceInfo* in = findById(captures, out.captureId);
        const DeviceInfo* outDev = findById(playbacks, out.playbackId);
        if (in != nullptr && outDev != nullptr) {
            auto lower = [](std::string x) {
                std::transform(x.begin(), x.end(), x.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return x;
            };
            const bool same = in->id == outDev->id && in->name == outDev->name;
            const bool monitor = lower(in->name) == "monitor of " + lower(outDev->name);
            if (same || monitor) {
                return Status::error("The input would capture the output (same device): choose "
                                     "a different input or output");
            }
        }
    }
    return Status::success();
}

bool Supervisor::tryStart(const SupervisorConfig& cfg) {
    teardown();
    setState(SupervisorState::Starting, "Starting\xE2\x80\xA6");

    std::unique_ptr<IAudioBackend> backend = factory_();
    if (!backend) {
        setState(SupervisorState::Failed, "No audio backend available", "no audio backend");
        return false;
    }

    OpenRequest req;
    Status st = resolveDevices(cfg, *backend, req);
    if (!st.ok) {
        // A bad configuration will not fix itself, but a missing cable or unplugged device might.
        const bool retry = st.message.find("No virtual audio cable") != std::string::npos ||
                           st.message.find("not available") != std::string::npos ||
                           st.message.find("No output device") != std::string::npos;
        setState(retry ? SupervisorState::Recovering : SupervisorState::Failed, st.message,
                 st.message);
        return false;
    }

    auto engine = std::make_unique<Engine>(params_);
    AudioCallbacks cb;
    cb.user = engine.get();
    cb.capture = [](void* u, const float* in, std::uint32_t n) {
        static_cast<Engine*>(u)->onCapture(in, n);
    };
    cb.playback = [](void* u, float* out, std::uint32_t n) {
        static_cast<Engine*>(u)->onPlayback(out, n);
    };

    st = backend->open(req, cb, &Supervisor::eventTrampoline, this);
    if (!st.ok) {
        setState(SupervisorState::Recovering, "Could not open audio devices: " + st.message,
                 st.message);
        return false;
    }

    NegotiatedInfo info = backend->info();
    if (cfg.captureLayout.n > 0) {
        ChannelMap m;
        m.n = info.captureMap.n;
        for (int i = 0; i < m.n; ++i) {
            m.pos[i] = i < cfg.captureLayout.n ? cfg.captureLayout.pos[i] : Pos::Unknown;
        }
        info.captureMap = m;
    }
    EngineConfig ec;
    ec.captureRate = info.captureRate;
    ec.playbackRate = info.playbackRate;
    ec.captureMap = info.captureMap;
    ec.captureBlock = info.capturePeriodFrames;
    ec.playbackBlock = info.playbackPeriodFrames;
    ec.latency = cfg.latency;
    ec.sharedClock = info.sharedClock;
    st = engine->configure(ec);
    if (st.ok && !cfg.tracePath.empty()) {
        engine->enableTrace(1u << 20);
    }
    if (st.ok) {
        st = backend->start();
    }
    if (!st.ok) {
        backend->close();
        setState(SupervisorState::Recovering, "Could not start audio: " + st.message, st.message);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(engineMu_);
        backend_ = std::move(backend);
        engine_ = std::move(engine);
        info_ = info;
        tracePath_ = cfg.tracePath;
    }
    setState(SupervisorState::Running, joinNames(info));
    return true;
}

void Supervisor::workerMain() {
    SupervisorConfig cfg;
    bool wantRunning = false;
    int attempt = 0;
    auto nextAttempt = Clock::now();
    auto reactAt = Clock::time_point::max(); // debounced reaction to a device event
    auto lastProgress = Clock::now();
    std::uint64_t lastCallbacks = 0;
    auto lastDevicePoll = Clock::now();
    bool nativeEvents = false;
    std::vector<DeviceInfo> lastCaptures;
    std::vector<DeviceInfo> lastPlaybacks;

    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mu_);
            // Sleep until something happens or a timer is due.
            auto wake = Clock::now() + std::chrono::milliseconds(100);
            if (state_ == SupervisorState::Recovering || state_ == SupervisorState::Starting) {
                wake = std::min(wake, nextAttempt);
            }
            if (reactAt != Clock::time_point::max()) {
                wake = std::min(wake, reactAt);
            }
            cv_.wait_until(lock, wake,
                           [this] { return quit_ || configChanged_ || !events_.empty(); });
            if (quit_) {
                break;
            }
            if (configChanged_) {
                configChanged_ = false;
                cfg = cfg_;
                wantRunning = true;
                attempt = 0;
                nextAttempt = Clock::now();
                reactAt = Clock::time_point::max();
                events_.clear();
                // Force an immediate (re)start below.
                state_ = SupervisorState::Recovering;
            }
            // Some events start (or extend) the debounce timer. Others are noise: a "device list
            // changed" fires whenever anything appears, including our own virtual sink, and the
            // periodic list comparison below catches real changes to the devices we care about.
            // A change of the system default output only matters if we were asked to follow it.
            if (state_ == SupervisorState::Running) {
                bool relevant = false;
                for (const DeviceEvent& e : events_) {
                    switch (e.kind) {
                    case DeviceEventKind::Stopped:
                    case DeviceEventKind::Removed:
                    case DeviceEventKind::FormatChanged:
                        relevant = true;
                        break;
                    case DeviceEventKind::DefaultChanged:
                        relevant = relevant || cfg.request.playbackId.empty();
                        break;
                    case DeviceEventKind::ListChanged:
                        break;
                    }
                }
                events_.clear();
                if (relevant) {
                    reactAt = Clock::now() + std::chrono::milliseconds(cfg.debounceMs);
                }
            } else {
                events_.clear();
            }
        }
        if (!wantRunning) {
            continue;
        }

        const auto now = Clock::now();
        const SupervisorState st = state();

        if (st == SupervisorState::Recovering && now >= nextAttempt) {
            if (tryStart(cfg)) {
                attempt = 0;
                lastProgress = Clock::now();
                lastCallbacks = 0;
                // Backends that push hot-plug events (PipeWire) are not polled: every poll would
                // open another connection to the audio server, which is wasted work and a source
                // of scheduling noise next to the audio threads.
                nativeEvents = backendHasNativeHotplug();
                if (!nativeEvents) {
                    lastCaptures = listDevices(Dir::Capture);
                    lastPlaybacks = listDevices(Dir::Playback);
                }
                lastDevicePoll = Clock::now();
            } else if (state() == SupervisorState::Failed) {
                wantRunning = false; // wait for the user to change the settings
            } else {
                const double ms =
                    std::min<double>(cfg.backoffMaxMs, cfg.backoffMinMs * std::pow(2.0, attempt));
                ++attempt;
                nextAttempt = Clock::now() + std::chrono::milliseconds(static_cast<int>(ms));
                restarts_.fetch_add(1);
            }
            continue;
        }

        if (st != SupervisorState::Running) {
            continue;
        }

        bool restart = false;
        std::string why;

        if (now >= reactAt) {
            restart = true;
            why = "Audio device changed";
            reactAt = Clock::time_point::max();
        }

        // Watchdog: playback callbacks must keep arriving.
        const EngineStats es = stats();
        if (es.playbackCallbacks != lastCallbacks) {
            lastCallbacks = es.playbackCallbacks;
            lastProgress = now;
        } else if (now - lastProgress > std::chrono::milliseconds(cfg.watchdogMs)) {
            restart = true;
            why = "Audio stopped responding";
        }

        // Hot-plug: compare the device lists, and react only if one of ours is gone or a new
        // device appeared that we might prefer.
        if (!restart && !nativeEvents && cfg.devicePollMs > 0 &&
            now - lastDevicePoll > std::chrono::milliseconds(cfg.devicePollMs)) {
            lastDevicePoll = now;
            const auto caps = listDevices(Dir::Capture);
            const auto plays = listDevices(Dir::Playback);
            auto changed = [](const std::vector<DeviceInfo>& a, const std::vector<DeviceInfo>& b) {
                if (a.size() != b.size()) {
                    return true;
                }
                for (std::size_t i = 0; i < a.size(); ++i) {
                    if (a[i].id != b[i].id || a[i].isDefault != b[i].isDefault) {
                        return true;
                    }
                }
                return false;
            };
            if (changed(caps, lastCaptures) || changed(plays, lastPlaybacks)) {
                lastCaptures = caps;
                lastPlaybacks = plays;
                reactAt = std::min(reactAt, now + std::chrono::milliseconds(cfg.debounceMs));
            }
        }

        if (restart) {
            // Fade out is handled by the engine on underrun; here we just tear down and retry.
            teardown();
            restarts_.fetch_add(1);
            setState(SupervisorState::Recovering, why + ", reconnecting\xE2\x80\xA6", why);
            attempt = 0;
            nextAttempt = Clock::now() + std::chrono::milliseconds(cfg.backoffMinMs);
        }
    }

    teardown();
}

} // namespace lsq
