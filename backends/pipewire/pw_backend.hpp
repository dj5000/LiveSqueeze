#pragma once

#include <memory>
#include <string>
#include <vector>

#include "lsq/audio_backend.hpp"

namespace lsq {

// Native PipeWire backend. Unlike the cable-based backends it creates the virtual device itself:
//
//   apps --> [LiveSqueeze sink, 5.1/7.1] --(our input stream)--> engine --(our output stream)-->
//   speakers
//
// The sink is a pw_stream whose node has media.class = Audio/Sink, so every PipeWire (and
// PulseAudio-compat) application can pick "LiveSqueeze" as its output device. Both streams share
// one node.group, so they run in the same graph cycle off the speakers' clock: there is no drift
// between them, and the adapters inside PipeWire do any rate or channel conversion.
class PipeWireBackend : public IAudioBackend {
public:
    PipeWireBackend();
    ~PipeWireBackend() override;

    const char* name() const override { return "pipewire"; }
    BackendCaps caps() const override { return {true, true, true}; }
    std::vector<DeviceInfo> enumerate(Dir dir) override;
    Status open(const OpenRequest& request, const AudioCallbacks& callbacks, EventCallback onEvent,
                void* eventUser) override;
    Status start() override;
    void stop() override;
    void close() override;
    NegotiatedInfo info() const override;

    // True if a PipeWire daemon accepts connections (a quick probe).
    static bool daemonReachable();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lsq
