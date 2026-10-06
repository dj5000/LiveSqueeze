#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "lsq/audio_backend.hpp"

namespace lsq {

// Audio I/O through miniaudio: WASAPI on Windows, CoreAudio on macOS, PulseAudio / ALSA / JACK on
// Linux. Capture and playback are two independent devices, each at its own native rate; the
// engine bridges them. This backend cannot create a virtual device: on Windows and macOS the
// user installs a virtual cable (VB-Cable, BlackHole) and we capture from it.
class MiniaudioBackend : public IAudioBackend {
public:
    struct Options {
        bool nullBackend = false; // miniaudio's software-timed "null" devices, for tests
    };

    MiniaudioBackend();
    explicit MiniaudioBackend(Options options);
    ~MiniaudioBackend() override;

    const char* name() const override { return "miniaudio"; }
    BackendCaps caps() const override { return {}; }
    std::vector<DeviceInfo> enumerate(Dir dir) override;
    Status open(const OpenRequest& request, const AudioCallbacks& callbacks, EventCallback onEvent,
                void* eventUser) override;
    Status start() override;
    void stop() override;
    void close() override;
    NegotiatedInfo info() const override { return info_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Options options_;
    NegotiatedInfo info_;
};

} // namespace lsq
