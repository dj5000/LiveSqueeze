#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lsq/channel_map.hpp"
#include "lsq/status.hpp"

namespace lsq {

enum class Dir { Capture, Playback };

struct DeviceInfo {
    std::string id;   // opaque, stable identifier understood by the same backend
    std::string name; // human readable
    int channels = 0; // native channel count (0 if unknown)
    bool isDefault = false;
    bool looksVirtual = false; // a virtual cable / loopback / monitor, by name
};

// True if the device name suggests a virtual cable or loopback ("CABLE", "BlackHole", ...).
bool nameLooksVirtual(const std::string& name);

struct OpenRequest {
    std::string captureId;  // "" = the backend's default capture device
    std::string playbackId; // "" = the system's default playback device
    // Backends that can create a virtual sink (PipeWire) do so and capture from it instead of
    // using captureId.
    bool createVirtualSink = false;
    std::string virtualSinkName = "LiveSqueeze";
    ChannelMap virtualSinkLayout = ChannelMap::standard(Layout::Surround51);
    std::uint32_t periodFrames = 0; // 0 = backend default
};

// What the devices actually agreed to.
struct NegotiatedInfo {
    double captureRate = 48000.0;
    double playbackRate = 48000.0;
    ChannelMap captureMap = ChannelMap::standard(Layout::Stereo);
    std::uint32_t capturePeriodFrames = 480;
    std::uint32_t playbackPeriodFrames = 480;
    double captureLatencyMs = 0.0;  // buffering inside the capture device/driver, if known
    double playbackLatencyMs = 0.0; // buffering inside the playback device/driver, if known
    std::string captureName;
    std::string playbackName;
    bool sharedClock = false; // both streams run off one clock (no drift expected)
};

// Callbacks run on the backend's audio threads. They must not block, allocate or throw.
struct AudioCallbacks {
    void* user = nullptr;
    void (*capture)(void* user, const float* interleaved, std::uint32_t frames) = nullptr;
    void (*playback)(void* user, float* stereo, std::uint32_t frames) = nullptr;
};

enum class DeviceEventKind {
    Stopped,        // a device stopped unexpectedly
    Removed,        // a device disappeared
    FormatChanged,  // rate or channel layout changed
    DefaultChanged, // the system default device changed
    ListChanged,    // devices were added or removed
};

struct DeviceEvent {
    DeviceEventKind kind = DeviceEventKind::Stopped;
};

// Called from backend threads; the receiver should only queue the event.
using EventCallback = void (*)(void* user, const DeviceEvent& event);

struct BackendCaps {
    bool createsVirtualSink = false;
    bool sharedClock = false;
    bool nativeHotplugEvents = false;
};

class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;

    virtual const char* name() const = 0;
    virtual BackendCaps caps() const = 0;
    virtual std::vector<DeviceInfo> enumerate(Dir dir) = 0;

    // Opens the devices without starting them. Playback is always stereo float. Capture is the
    // device's native rate and channel count; no remixing or resampling is done on that side.
    virtual Status open(const OpenRequest& request, const AudioCallbacks& callbacks,
                        EventCallback onEvent, void* eventUser) = 0;
    virtual Status start() = 0;
    virtual void stop() = 0; // returns only once no callback is running
    virtual void close() = 0;
    virtual NegotiatedInfo info() const = 0;
};

} // namespace lsq
