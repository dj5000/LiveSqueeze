#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "lsq/audio_backend.hpp"
#include "lsq/engine.hpp"

namespace lsq {

enum class SupervisorState {
    Idle,       // not started
    Starting,   // opening devices
    Running,    // audio is flowing
    Recovering, // something went wrong; retrying with backoff
    Failed,     // the configuration cannot work; waiting for the user to change it
};

const char* stateName(SupervisorState s) noexcept;

struct SupervisorConfig {
    OpenRequest request;
    LatencyMode latency = LatencyMode::Balanced;
    // Which speakers the first channels of the input carry, for virtual cables that cannot say
    // (BlackHole 16ch with 5.1 in the first six channels). n == 0 uses what the device reports.
    ChannelMap captureLayout{};
    // If not empty, the timing of every audio callback is written to this CSV file when audio
    // stops (time, kind, frames, fill, trim), for diagnosing glitches.
    std::string tracePath;

    int backoffMinMs = 250; // first retry delay; doubles up to backoffMaxMs
    int backoffMaxMs = 5000;
    int debounceMs = 250;    // wait this long after a device event before reacting
    int watchdogMs = 1500;   // restart if no playback callback arrives for this long
    int devicePollMs = 2000; // how often to compare device lists (0 = never)
};

using BackendFactory = std::function<std::unique_ptr<IAudioBackend>()>;

// Owns the backend and the engine and keeps them running. A worker thread opens the devices,
// restarts them when a device disappears, changes format or stalls (with exponential backoff and
// a watchdog), and refuses combinations that would feed the output back into the input.
//
// All public methods may be called from any thread except the audio callbacks.
class Supervisor {
public:
    Supervisor(BackendFactory factory, ParamStore* params);
    ~Supervisor();
    Supervisor(const Supervisor&) = delete;
    Supervisor& operator=(const Supervisor&) = delete;

    // Starts (or restarts with new settings) asynchronously.
    void start(const SupervisorConfig& cfg);
    // Stops audio and joins the worker. The supervisor can be started again.
    void stop();

    SupervisorState state() const;
    std::string statusText() const; // short, human readable
    std::string lastError() const;
    EngineStats stats() const; // zeros while there is no engine
    NegotiatedInfo negotiated() const;
    std::uint64_t restarts() const { return restarts_.load(); }
    bool popMeter(MeterFrame& out);

    // Devices as the backend sees them (a throw-away backend instance is used).
    std::vector<DeviceInfo> listDevices(Dir dir);

    // Called from the worker thread whenever the state or status text changes.
    void setStateCallback(std::function<void(SupervisorState, const std::string&)> cb);

private:
    void workerMain();
    bool tryStart(const SupervisorConfig& cfg);
    void teardown();
    void writeTrace() const; // engineMu_ must be held
    bool backendHasNativeHotplug() const;
    void setState(SupervisorState s, std::string status, std::string error = {});
    void postEvent(const DeviceEvent& e);
    static void eventTrampoline(void* user, const DeviceEvent& e);
    Status resolveDevices(const SupervisorConfig& cfg, IAudioBackend& backend, OpenRequest& out);

    BackendFactory factory_;
    ParamStore* params_;

    mutable std::mutex mu_; // guards the fields below and the condition variable
    std::condition_variable cv_;
    std::thread worker_;
    bool quit_ = false;
    bool configChanged_ = false;
    SupervisorConfig cfg_;
    std::deque<DeviceEvent> events_;
    SupervisorState state_ = SupervisorState::Idle;
    std::string status_ = "Stopped";
    std::string error_;
    std::function<void(SupervisorState, const std::string&)> stateCb_;

    // Worker-owned, but read by stats()/popMeter() under engineMu_.
    mutable std::mutex engineMu_;
    std::unique_ptr<IAudioBackend> backend_;
    std::unique_ptr<Engine> engine_;
    NegotiatedInfo info_;
    std::string tracePath_;

    std::atomic<std::uint64_t> restarts_{0};
};

} // namespace lsq
