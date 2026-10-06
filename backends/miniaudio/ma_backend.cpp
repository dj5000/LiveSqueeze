#include "ma_backend.hpp"

#include <cstring>
#include <mutex>

#include "miniaudio.h"

namespace lsq {
namespace {

std::string toHex(const ma_device_id& id) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    const auto* p = reinterpret_cast<const unsigned char*>(&id);
    for (std::size_t i = 0; i < sizeof(ma_device_id); ++i) {
        s += digits[p[i] >> 4];
        s += digits[p[i] & 15];
    }
    return s;
}

bool fromHex(const std::string& s, ma_device_id& out) {
    if (s.size() != 2 * sizeof(ma_device_id)) {
        return false;
    }
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    auto* p = reinterpret_cast<unsigned char*>(&out);
    for (std::size_t i = 0; i < sizeof(ma_device_id); ++i) {
        const int hi = nibble(s[2 * i]);
        const int lo = nibble(s[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        p[i] = static_cast<unsigned char>(hi * 16 + lo);
    }
    return true;
}

Pos fromMiniaudio(ma_channel c) {
    switch (c) {
    case MA_CHANNEL_MONO:
        return Pos::FC;
    case MA_CHANNEL_FRONT_LEFT:
        return Pos::FL;
    case MA_CHANNEL_FRONT_RIGHT:
        return Pos::FR;
    case MA_CHANNEL_FRONT_CENTER:
        return Pos::FC;
    case MA_CHANNEL_LFE:
        return Pos::LFE;
    case MA_CHANNEL_BACK_LEFT:
        return Pos::BL;
    case MA_CHANNEL_BACK_RIGHT:
        return Pos::BR;
    case MA_CHANNEL_FRONT_LEFT_CENTER:
        return Pos::FLC;
    case MA_CHANNEL_FRONT_RIGHT_CENTER:
        return Pos::FRC;
    case MA_CHANNEL_BACK_CENTER:
        return Pos::BC;
    case MA_CHANNEL_SIDE_LEFT:
        return Pos::SL;
    case MA_CHANNEL_SIDE_RIGHT:
        return Pos::SR;
    default:
        return Pos::Unknown;
    }
}

ChannelMap mapFromDevice(const ma_device& dev) {
    const int n = static_cast<int>(dev.capture.channels);
    ChannelMap m;
    m.n = static_cast<std::uint8_t>(std::min(n, kMaxChannels));
    bool anyKnown = false;
    for (int i = 0; i < m.n; ++i) {
        m.pos[i] = fromMiniaudio(dev.capture.channelMap[i]);
        anyKnown = anyKnown || m.pos[i] != Pos::Unknown;
    }
    // Drivers that report no labels at all get the conventional layout for their channel count.
    return anyKnown ? m : ChannelMap::fromCount(n);
}

} // namespace

struct MiniaudioBackend::Impl {
    ma_context context{};
    bool contextReady = false;
    ma_device capture{};
    ma_device playback{};
    bool captureReady = false;
    bool playbackReady = false;
    AudioCallbacks cb{};
    EventCallback onEvent = nullptr;
    void* eventUser = nullptr;
    std::atomic<bool> running{false};

    static void captureData(ma_device* d, void*, const void* in, ma_uint32 frames) {
        auto* self = static_cast<Impl*>(d->pUserData);
        if (self->cb.capture != nullptr) {
            self->cb.capture(self->cb.user, static_cast<const float*>(in), frames);
        }
    }

    static void playbackData(ma_device* d, void* out, const void*, ma_uint32 frames) {
        auto* self = static_cast<Impl*>(d->pUserData);
        if (self->cb.playback != nullptr) {
            self->cb.playback(self->cb.user, static_cast<float*>(out), frames);
        } else {
            std::memset(out, 0, sizeof(float) * 2 * frames);
        }
    }

    static void notify(const ma_device_notification* n) {
        auto* self = static_cast<Impl*>(n->pDevice->pUserData);
        if (self->onEvent == nullptr || !self->running.load()) {
            return; // ignore the notifications caused by our own start/stop
        }
        DeviceEvent e;
        switch (n->type) {
        case ma_device_notification_type_stopped:
            e.kind = DeviceEventKind::Stopped;
            break;
        case ma_device_notification_type_rerouted:
            e.kind = DeviceEventKind::DefaultChanged;
            break;
        default:
            return;
        }
        self->onEvent(self->eventUser, e);
    }

    bool initContext(bool nullBackend, std::string& error) {
        if (contextReady) {
            return true;
        }
        // Real use never falls back to miniaudio's "null" backend: with no audio system the user
        // should see an error, not silent fake devices.
        ma_backend backends[MA_BACKEND_COUNT];
        std::size_t count = 0;
        if (nullBackend) {
            backends[count++] = ma_backend_null;
        } else {
            ma_backend all[MA_BACKEND_COUNT];
            std::size_t enabled = 0;
            ma_get_enabled_backends(all, MA_BACKEND_COUNT, &enabled);
            for (std::size_t i = 0; i < enabled; ++i) {
                if (all[i] != ma_backend_null) {
                    backends[count++] = all[i];
                }
            }
        }
        const ma_result r =
            ma_context_init(backends, static_cast<ma_uint32>(count), nullptr, &context);
        if (r != MA_SUCCESS) {
            error = std::string("no audio system available (") + ma_result_description(r) + ")";
            return false;
        }
        contextReady = true;
        return true;
    }

    void closeDevices() {
        if (captureReady) {
            ma_device_uninit(&capture);
            captureReady = false;
        }
        if (playbackReady) {
            ma_device_uninit(&playback);
            playbackReady = false;
        }
    }
};

MiniaudioBackend::MiniaudioBackend() : MiniaudioBackend(Options{}) {}

MiniaudioBackend::MiniaudioBackend(Options options)
    : impl_(std::make_unique<Impl>()), options_(options) {}

MiniaudioBackend::~MiniaudioBackend() {
    close();
    if (impl_->contextReady) {
        ma_context_uninit(&impl_->context);
    }
}

std::vector<DeviceInfo> MiniaudioBackend::enumerate(Dir dir) {
    std::vector<DeviceInfo> out;
    std::string err;
    if (!impl_->initContext(options_.nullBackend, err)) {
        return out;
    }
    ma_device_info* playbacks = nullptr;
    ma_uint32 nPlayback = 0;
    ma_device_info* captures = nullptr;
    ma_uint32 nCapture = 0;
    if (ma_context_get_devices(&impl_->context, &playbacks, &nPlayback, &captures, &nCapture) !=
        MA_SUCCESS) {
        return out;
    }
    const ma_device_info* list = dir == Dir::Capture ? captures : playbacks;
    const ma_uint32 count = dir == Dir::Capture ? nCapture : nPlayback;
    for (ma_uint32 i = 0; i < count; ++i) {
        DeviceInfo d;
        d.id = toHex(list[i].id);
        d.name = list[i].name;
        d.isDefault = list[i].isDefault != 0;
        // In the test (null) backend the capture device plays the part of a virtual cable.
        d.looksVirtual = options_.nullBackend ? dir == Dir::Capture : nameLooksVirtual(d.name);

        ma_device_info detail{};
        if (ma_context_get_device_info(&impl_->context,
                                       dir == Dir::Capture ? ma_device_type_capture
                                                           : ma_device_type_playback,
                                       &list[i].id, &detail) == MA_SUCCESS &&
            detail.nativeDataFormatCount > 0) {
            d.channels = static_cast<int>(detail.nativeDataFormats[0].channels);
        }
        out.push_back(std::move(d));
    }
    return out;
}

Status MiniaudioBackend::open(const OpenRequest& req, const AudioCallbacks& callbacks,
                              EventCallback onEvent, void* eventUser) {
    close();
    std::string err;
    if (!impl_->initContext(options_.nullBackend, err)) {
        return Status::error(err);
    }
    Impl& m = *impl_;
    m.cb = callbacks;
    m.onEvent = onEvent;
    m.eventUser = eventUser;

    ma_device_id captureId{};
    ma_device_id playbackId{};
    const bool haveCaptureId = !req.captureId.empty() && fromHex(req.captureId, captureId);
    const bool havePlaybackId = !req.playbackId.empty() && fromHex(req.playbackId, playbackId);
    if (!req.captureId.empty() && !haveCaptureId) {
        return Status::error("unrecognised capture device id");
    }
    if (!req.playbackId.empty() && !havePlaybackId) {
        return Status::error("unrecognised playback device id");
    }

    // Capture: the device's own format, no remixing or resampling.
    {
        ma_device_config c = ma_device_config_init(ma_device_type_capture);
        c.capture.pDeviceID = haveCaptureId ? &captureId : nullptr;
        c.capture.format = ma_format_f32;
        c.capture.channels = 0;
        c.sampleRate = 0;
        c.periodSizeInFrames = req.periodFrames;
        c.performanceProfile = ma_performance_profile_low_latency;
        c.dataCallback = &Impl::captureData;
        c.notificationCallback = &Impl::notify;
        c.pUserData = &m;
        const ma_result r = ma_device_init(&m.context, &c, &m.capture);
        if (r != MA_SUCCESS) {
            return Status::error(std::string("cannot open the input device: ") +
                                 ma_result_description(r));
        }
        m.captureReady = true;
    }

    // Playback: stereo float at the device's native rate.
    {
        ma_device_config c = ma_device_config_init(ma_device_type_playback);
        c.playback.pDeviceID = havePlaybackId ? &playbackId : nullptr;
        c.playback.format = ma_format_f32;
        c.playback.channels = 2;
        c.sampleRate = 0;
        c.periodSizeInFrames = req.periodFrames;
        c.performanceProfile = ma_performance_profile_low_latency;
        c.dataCallback = &Impl::playbackData;
        c.notificationCallback = &Impl::notify;
        c.pUserData = &m;
        const ma_result r = ma_device_init(&m.context, &c, &m.playback);
        if (r != MA_SUCCESS) {
            m.closeDevices();
            return Status::error(std::string("cannot open the output device: ") +
                                 ma_result_description(r));
        }
        m.playbackReady = true;
    }

    info_ = NegotiatedInfo{};
    info_.captureRate = static_cast<double>(m.capture.sampleRate);
    info_.playbackRate = static_cast<double>(m.playback.sampleRate);
    info_.captureMap = mapFromDevice(m.capture);
    info_.capturePeriodFrames = m.capture.capture.internalPeriodSizeInFrames;
    info_.playbackPeriodFrames = m.playback.playback.internalPeriodSizeInFrames;
    info_.captureName = m.capture.capture.name;
    info_.playbackName = m.playback.playback.name;
    const double capBuf = static_cast<double>(m.capture.capture.internalPeriodSizeInFrames) *
                          m.capture.capture.internalPeriods;
    const double playBuf = static_cast<double>(m.playback.playback.internalPeriodSizeInFrames) *
                           m.playback.playback.internalPeriods;
    info_.captureLatencyMs =
        capBuf / static_cast<double>(m.capture.capture.internalSampleRate) * 1000.0;
    info_.playbackLatencyMs =
        playBuf / static_cast<double>(m.playback.playback.internalSampleRate) * 1000.0;
    return Status::success();
}

Status MiniaudioBackend::start() {
    Impl& m = *impl_;
    if (!m.captureReady || !m.playbackReady) {
        return Status::error("devices are not open");
    }
    // Capture first so there is data by the time playback asks for it.
    ma_result r = ma_device_start(&m.capture);
    if (r != MA_SUCCESS) {
        return Status::error(std::string("cannot start the input device: ") +
                             ma_result_description(r));
    }
    r = ma_device_start(&m.playback);
    if (r != MA_SUCCESS) {
        ma_device_stop(&m.capture);
        return Status::error(std::string("cannot start the output device: ") +
                             ma_result_description(r));
    }
    m.running = true;
    return Status::success();
}

void MiniaudioBackend::stop() {
    Impl& m = *impl_;
    m.running = false;
    if (m.playbackReady) {
        ma_device_stop(&m.playback); // blocks until the callback has returned
    }
    if (m.captureReady) {
        ma_device_stop(&m.capture);
    }
}

void MiniaudioBackend::close() {
    stop();
    impl_->closeDevices();
}

} // namespace lsq
