#include "pw_backend.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

namespace lsq {
namespace {

struct SinkEntry {
    std::uint32_t id = 0;
    std::string name;        // node.name, used as our device id
    std::string description; // shown to the user
    bool isVirtual = false;
    int priority = 0;
};

const char* lookup(const spa_dict* props, const char* key) {
    return props != nullptr ? spa_dict_lookup(props, key) : nullptr;
}

std::uint32_t toSpaChannel(Pos p) {
    switch (p) {
    case Pos::FL:
        return SPA_AUDIO_CHANNEL_FL;
    case Pos::FR:
        return SPA_AUDIO_CHANNEL_FR;
    case Pos::FC:
        return SPA_AUDIO_CHANNEL_FC;
    case Pos::LFE:
        return SPA_AUDIO_CHANNEL_LFE;
    case Pos::BL:
        return SPA_AUDIO_CHANNEL_RL;
    case Pos::BR:
        return SPA_AUDIO_CHANNEL_RR;
    case Pos::SL:
        return SPA_AUDIO_CHANNEL_SL;
    case Pos::SR:
        return SPA_AUDIO_CHANNEL_SR;
    case Pos::FLC:
        return SPA_AUDIO_CHANNEL_FLC;
    case Pos::FRC:
        return SPA_AUDIO_CHANNEL_FRC;
    case Pos::BC:
        return SPA_AUDIO_CHANNEL_RC;
    case Pos::Unknown:
        return SPA_AUDIO_CHANNEL_UNKNOWN;
    }
    return SPA_AUDIO_CHANNEL_UNKNOWN;
}

const char* spaPositionName(Pos p) {
    switch (p) {
    case Pos::FL:
        return "FL";
    case Pos::FR:
        return "FR";
    case Pos::FC:
        return "FC";
    case Pos::LFE:
        return "LFE";
    case Pos::BL:
        return "RL";
    case Pos::BR:
        return "RR";
    case Pos::SL:
        return "SL";
    case Pos::SR:
        return "SR";
    case Pos::FLC:
        return "FLC";
    case Pos::FRC:
        return "FRC";
    case Pos::BC:
        return "RC";
    case Pos::Unknown:
        return "UNK";
    }
    return "UNK";
}

// Pulls "name" out of a metadata value such as
// {"name":"alsa_output.pci-0000_00_1f.3.analog-stereo"}.
std::string jsonName(const char* value) {
    if (value == nullptr) {
        return {};
    }
    const std::string v(value);
    const std::size_t k = v.find("\"name\"");
    if (k == std::string::npos) {
        return {};
    }
    const std::size_t open = v.find('"', v.find(':', k) + 1);
    const std::size_t close = open == std::string::npos ? open : v.find('"', open + 1);
    return close == std::string::npos ? std::string{} : v.substr(open + 1, close - open - 1);
}

constexpr const char* kSinkNodeName = "livesqueeze";

} // namespace

// A connection to the PipeWire daemon with a registry listener that tracks audio sinks and the
// default sink. Used both for enumeration and, kept alive, for the running backend.
struct Session {
    pw_thread_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_registry* registry = nullptr;
    pw_metadata* metadata = nullptr;
    spa_hook coreHook{};
    spa_hook registryHook{};
    spa_hook metadataHook{};

    std::vector<SinkEntry> sinks;
    std::string defaultSink;
    int pendingSeq = 0;
    bool synced = false;
    bool failed = false;

    // Set while a run is active; called on the PipeWire loop thread.
    EventCallback onEvent = nullptr;
    void* eventUser = nullptr;
    std::string watchedTarget; // node.name of the output we are playing to
    std::uint32_t watchedId = SPA_ID_INVALID;

    static void coreDone(void* data, std::uint32_t id, int seq) {
        auto* s = static_cast<Session*>(data);
        if (id == PW_ID_CORE && seq == s->pendingSeq) {
            s->synced = true;
            pw_thread_loop_signal(s->loop, false);
        }
    }

    static void coreError(void* data, std::uint32_t id, int, int res, const char*) {
        auto* s = static_cast<Session*>(data);
        if (id == PW_ID_CORE && (res == -EPIPE || res == -ECONNRESET)) {
            s->failed = true;
            pw_thread_loop_signal(s->loop, false);
            s->emit(DeviceEventKind::Stopped); // the daemon went away
        }
    }

    static void registryGlobal(void* data, std::uint32_t id, std::uint32_t, const char* type,
                               std::uint32_t, const spa_dict* props) {
        auto* s = static_cast<Session*>(data);
        if (props == nullptr) {
            return;
        }
        if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
            const char* mediaClass = lookup(props, PW_KEY_MEDIA_CLASS);
            if (mediaClass == nullptr || std::strcmp(mediaClass, "Audio/Sink") != 0) {
                return;
            }
            SinkEntry e;
            e.id = id;
            if (const char* n = lookup(props, PW_KEY_NODE_NAME))
                e.name = n;
            if (const char* d = lookup(props, PW_KEY_NODE_DESCRIPTION))
                e.description = d;
            else if (const char* nick = lookup(props, PW_KEY_NODE_NICK))
                e.description = nick;
            else
                e.description = e.name;
            const char* virt = lookup(props, PW_KEY_NODE_VIRTUAL);
            const char* factory = lookup(props, PW_KEY_FACTORY_NAME);
            e.isVirtual =
                (virt != nullptr && std::strcmp(virt, "true") == 0) ||
                (factory != nullptr && std::strcmp(factory, "support.null-audio-sink") == 0) ||
                e.name == kSinkNodeName;
            if (const char* prio = lookup(props, PW_KEY_PRIORITY_SESSION))
                e.priority = std::atoi(prio);
            if (e.name == s->watchedTarget) {
                s->watchedId = id;
            }
            s->sinks.push_back(std::move(e));
            s->emit(DeviceEventKind::ListChanged);
        } else if (std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
            const char* name = lookup(props, PW_KEY_METADATA_NAME);
            if (name != nullptr && std::strcmp(name, "default") == 0 && s->metadata == nullptr) {
                s->metadata = static_cast<pw_metadata*>(
                    pw_registry_bind(s->registry, id, type, PW_VERSION_METADATA, 0));
                if (s->metadata != nullptr) {
                    static const pw_metadata_events events = {
                        PW_VERSION_METADATA_EVENTS,
                        &Session::metadataProperty,
                    };
                    pw_metadata_add_listener(s->metadata, &s->metadataHook, &events, s);
                }
            }
        }
    }

    static void registryGlobalRemove(void* data, std::uint32_t id) {
        auto* s = static_cast<Session*>(data);
        const auto it = std::find_if(s->sinks.begin(), s->sinks.end(),
                                     [id](const SinkEntry& e) { return e.id == id; });
        if (it == s->sinks.end()) {
            return;
        }
        const bool wasTarget = id == s->watchedId;
        s->sinks.erase(it);
        s->emit(wasTarget ? DeviceEventKind::Removed : DeviceEventKind::ListChanged);
    }

    static int metadataProperty(void* data, std::uint32_t, const char* key, const char*,
                                const char* value) {
        auto* s = static_cast<Session*>(data);
        if (key != nullptr && std::strcmp(key, "default.audio.sink") == 0) {
            s->defaultSink = jsonName(value);
        }
        return 0;
    }

    void emit(DeviceEventKind kind) {
        if (onEvent != nullptr) {
            onEvent(eventUser, DeviceEvent{kind});
        }
    }

    // Connects and waits until the existing sinks and the default-sink metadata are known.
    bool connect(std::string& error) {
        pw_init(nullptr, nullptr);
        loop = pw_thread_loop_new("lsq-pipewire", nullptr);
        if (loop == nullptr) {
            error = "cannot create the PipeWire event loop";
            return false;
        }
        context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
        if (context == nullptr || pw_thread_loop_start(loop) < 0) {
            error = "cannot start the PipeWire event loop";
            disconnect();
            return false;
        }
        pw_thread_loop_lock(loop);
        core = pw_context_connect(context, nullptr, 0);
        if (core == nullptr) {
            pw_thread_loop_unlock(loop);
            error = "cannot connect to the PipeWire daemon (is PipeWire running?)";
            disconnect();
            return false;
        }
        static const pw_core_events coreEvents = {
            PW_VERSION_CORE_EVENTS, nullptr, &Session::coreDone, nullptr, &Session::coreError,
        };
        pw_core_add_listener(core, &coreHook, &coreEvents, this);
        registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
        static const pw_registry_events registryEvents = {
            PW_VERSION_REGISTRY_EVENTS,
            &Session::registryGlobal,
            &Session::registryGlobalRemove,
        };
        pw_registry_add_listener(registry, &registryHook, &registryEvents, this);

        // First round trip delivers the globals; binding the metadata object needs a second one
        // to receive its properties.
        const bool ok = roundtrip(3) && roundtrip(2);
        pw_thread_loop_unlock(loop);
        if (!ok) {
            error = "the PipeWire daemon did not respond";
            disconnect();
            return false;
        }
        return true;
    }

    // Loop must be locked.
    bool roundtrip(int seconds) {
        synced = false;
        pendingSeq = pw_core_sync(core, PW_ID_CORE, pendingSeq);
        while (!synced && !failed) {
            if (pw_thread_loop_timed_wait(loop, seconds) != 0) {
                return false;
            }
        }
        return !failed;
    }

    void disconnect() {
        if (loop != nullptr) {
            pw_thread_loop_lock(loop);
            if (metadata != nullptr) {
                spa_hook_remove(&metadataHook);
                pw_proxy_destroy(reinterpret_cast<pw_proxy*>(metadata));
                metadata = nullptr;
            }
            if (registry != nullptr) {
                spa_hook_remove(&registryHook);
                pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
                registry = nullptr;
            }
            if (core != nullptr) {
                spa_hook_remove(&coreHook);
                pw_core_disconnect(core);
                core = nullptr;
            }
            pw_thread_loop_unlock(loop);
            pw_thread_loop_stop(loop);
        }
        if (context != nullptr) {
            pw_context_destroy(context);
            context = nullptr;
        }
        if (loop != nullptr) {
            pw_thread_loop_destroy(loop);
            loop = nullptr;
        }
    }

    // Snapshot for callers outside the loop thread.
    std::vector<SinkEntry> snapshotSinks(std::string* defaultName) {
        pw_thread_loop_lock(loop);
        std::vector<SinkEntry> copy = sinks;
        if (defaultName != nullptr) {
            *defaultName = defaultSink;
        }
        pw_thread_loop_unlock(loop);
        return copy;
    }
};

struct PipeWireBackend::Impl {
    Session session;
    bool sessionReady = false;

    AudioCallbacks cb{};
    pw_stream* capture = nullptr;  // our sink: receives what applications play
    pw_stream* playback = nullptr; // plays the processed stereo to the chosen output
    spa_hook captureHook{};
    spa_hook playbackHook{};
    bool started = false;
    std::atomic<bool> running{false};

    NegotiatedInfo info;
    int captureChannels = 0;
    std::vector<float> silence; // for buffers PipeWire flags as empty

    static void post(Impl* self, DeviceEventKind kind) {
        if (self->session.onEvent != nullptr && self->running.load()) {
            self->session.onEvent(self->session.eventUser, DeviceEvent{kind});
        }
    }

    // ---- real-time callbacks ------------------------------------------------------------

    static void captureProcess(void* data) {
        auto* self = static_cast<Impl*>(data);
        pw_buffer* b = pw_stream_dequeue_buffer(self->capture);
        if (b == nullptr) {
            return;
        }
        const spa_data& d = b->buffer->datas[0];
        if (d.data != nullptr && d.chunk != nullptr && self->cb.capture != nullptr) {
            const std::uint32_t stride =
                sizeof(float) * static_cast<std::uint32_t>(self->captureChannels);
            const std::uint32_t offset = std::min(d.chunk->offset, d.maxsize);
            const std::uint32_t size = std::min(d.chunk->size, d.maxsize - offset);
            std::uint32_t frames = size / stride;
            const float* samples =
                reinterpret_cast<const float*>(static_cast<const std::uint8_t*>(d.data) + offset);
            if ((d.chunk->flags & SPA_CHUNK_FLAG_EMPTY) != 0) {
                frames = std::min<std::uint32_t>(
                    frames, static_cast<std::uint32_t>(self->silence.size()) /
                                static_cast<std::uint32_t>(self->captureChannels));
                samples = self->silence.data();
            }
            if (frames > 0) {
                self->cb.capture(self->cb.user, samples, frames);
            }
        }
        pw_stream_queue_buffer(self->capture, b);
    }

    static void playbackProcess(void* data) {
        auto* self = static_cast<Impl*>(data);
        pw_buffer* b = pw_stream_dequeue_buffer(self->playback);
        if (b == nullptr) {
            return;
        }
        spa_data& d = b->buffer->datas[0];
        if (d.data == nullptr) {
            pw_stream_queue_buffer(self->playback, b);
            return;
        }
        constexpr std::uint32_t stride = sizeof(float) * 2;
        std::uint32_t frames = d.maxsize / stride;
        if (b->requested > 0) {
            frames = std::min<std::uint32_t>(frames, static_cast<std::uint32_t>(b->requested));
        }
        if (self->cb.playback != nullptr) {
            self->cb.playback(self->cb.user, static_cast<float*>(d.data), frames);
        } else {
            std::memset(d.data, 0, frames * stride);
        }
        d.chunk->offset = 0;
        d.chunk->stride = static_cast<std::int32_t>(stride);
        d.chunk->size = frames * stride;
        pw_stream_queue_buffer(self->playback, b);
    }

    // ---- main-loop callbacks -----------------------------------------------------------

    static void streamState(void* data, pw_stream_state, pw_stream_state state, const char*) {
        auto* self = static_cast<Impl*>(data);
        if (state == PW_STREAM_STATE_ERROR) {
            post(self, DeviceEventKind::Stopped);
        }
    }

    void destroyStreams() {
        if (session.loop == nullptr) {
            return;
        }
        pw_thread_loop_lock(session.loop);
        if (capture != nullptr) {
            spa_hook_remove(&captureHook);
            pw_stream_destroy(capture);
            capture = nullptr;
        }
        if (playback != nullptr) {
            spa_hook_remove(&playbackHook);
            pw_stream_destroy(playback);
            playback = nullptr;
        }
        pw_thread_loop_unlock(session.loop);
    }
};

PipeWireBackend::PipeWireBackend() : impl_(std::make_unique<Impl>()) {}

PipeWireBackend::~PipeWireBackend() {
    close();
}

bool PipeWireBackend::daemonReachable() {
    Session probe;
    std::string error;
    const bool ok = probe.connect(error);
    probe.disconnect();
    return ok;
}

std::vector<DeviceInfo> PipeWireBackend::enumerate(Dir dir) {
    std::vector<DeviceInfo> out;
    if (dir == Dir::Capture) {
        // The capture side is the virtual sink we create ourselves.
        DeviceInfo d;
        d.id = "virtual-sink";
        d.name = "LiveSqueeze virtual sink (created automatically)";
        d.isDefault = true;
        d.looksVirtual = true;
        out.push_back(std::move(d));
        return out;
    }

    Impl& m = *impl_;
    Session temp;
    Session* s = &m.session;
    if (!m.sessionReady) {
        std::string error;
        if (!temp.connect(error)) {
            return out;
        }
        s = &temp;
    }
    std::string defaultName;
    std::vector<SinkEntry> sinks = s->snapshotSinks(&defaultName);
    if (s == &temp) {
        temp.disconnect();
    }

    // Best candidate first: the default sink if it is a real device, then by session priority.
    std::stable_sort(sinks.begin(), sinks.end(), [&](const SinkEntry& a, const SinkEntry& b) {
        if (a.isVirtual != b.isVirtual)
            return !a.isVirtual;
        const bool ad = a.name == defaultName;
        const bool bd = b.name == defaultName;
        if (ad != bd)
            return ad;
        return a.priority > b.priority;
    });
    bool markedDefault = false;
    for (const SinkEntry& e : sinks) {
        DeviceInfo d;
        d.id = e.name;
        d.name = e.description;
        d.looksVirtual = e.isVirtual;
        d.isDefault = !e.isVirtual && !markedDefault;
        markedDefault = markedDefault || d.isDefault;
        out.push_back(std::move(d));
    }
    return out;
}

Status PipeWireBackend::open(const OpenRequest& req, const AudioCallbacks& callbacks,
                             EventCallback onEvent, void* eventUser) {
    close();
    Impl& m = *impl_;
    m.cb = callbacks;

    std::string error;
    if (!m.session.connect(error)) {
        return Status::error(error);
    }
    m.sessionReady = true;
    m.session.onEvent = onEvent;
    m.session.eventUser = eventUser;

    // Choose the real output to play to.
    std::string defaultName;
    const std::vector<SinkEntry> sinks = m.session.snapshotSinks(&defaultName);
    const SinkEntry* target = nullptr;
    if (!req.playbackId.empty()) {
        for (const SinkEntry& e : sinks) {
            if (e.name == req.playbackId) {
                target = &e;
            }
        }
        if (target == nullptr) {
            close();
            return Status::error("the chosen output device is not available");
        }
    } else {
        for (const SinkEntry& e : sinks) {
            if (!e.isVirtual && e.name == defaultName) {
                target = &e;
            }
        }
        for (const SinkEntry& e : sinks) {
            if (target == nullptr ||
                (!e.isVirtual && e.priority > target->priority && e.name != defaultName)) {
                if (!e.isVirtual) {
                    target = &e;
                }
            }
        }
        if (target == nullptr) {
            close();
            return Status::error("no output device found");
        }
    }
    if (target->isVirtual) {
        close();
        return Status::error(
            "the output device is a virtual sink; choose your speakers or headphones");
    }
    m.session.watchedTarget = target->name;
    m.session.watchedId = target->id;

    // The virtual sink's layout, e.g. 5.1.
    ChannelMap layout = req.virtualSinkLayout;
    if (layout.n < 1) {
        layout = ChannelMap::standard(Layout::Surround51);
    }
    const int channels = layout.n;
    m.captureChannels = channels;
    const std::uint32_t period = req.periodFrames > 0 ? req.periodFrames : 512;
    const double rate = 48000.0;
    m.silence.assign(static_cast<std::size_t>(16384) * static_cast<std::size_t>(channels), 0.0f);

    char latency[32];
    std::snprintf(latency, sizeof latency, "%u/48000", period);
    std::string positions;
    for (int i = 0; i < channels; ++i) {
        positions += (i > 0 ? "," : "");
        positions += spaPositionName(layout.pos[i]);
    }
    char channelCount[16];
    std::snprintf(channelCount, sizeof channelCount, "%d", channels);

    pw_thread_loop_lock(m.session.loop);

    // Our sink: a stream whose node is an Audio/Sink.
    pw_properties* sinkProps = pw_properties_new(
        PW_KEY_MEDIA_CLASS, "Audio/Sink", PW_KEY_NODE_NAME, kSinkNodeName, PW_KEY_NODE_DESCRIPTION,
        req.virtualSinkName.c_str(), PW_KEY_NODE_VIRTUAL, "true", PW_KEY_NODE_GROUP, "livesqueeze",
        PW_KEY_NODE_LATENCY, latency, PW_KEY_AUDIO_CHANNELS, channelCount, SPA_KEY_AUDIO_POSITION,
        positions.c_str(), PW_KEY_MEDIA_NAME, "LiveSqueeze input", nullptr);
    m.capture = pw_stream_new(m.session.core, "LiveSqueeze input", sinkProps);

    // The output stream, pinned to the chosen real device so it can never loop back into ours.
    pw_properties* outProps = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback", PW_KEY_MEDIA_ROLE, "Music",
        PW_KEY_NODE_NAME, "livesqueeze_output", PW_KEY_NODE_DESCRIPTION, "LiveSqueeze output",
        PW_KEY_NODE_GROUP, "livesqueeze", PW_KEY_NODE_LATENCY, latency, PW_KEY_TARGET_OBJECT,
        target->name.c_str(), "node.dont-fallback", "true", PW_KEY_NODE_DONT_RECONNECT, "true",
        PW_KEY_MEDIA_NAME, "LiveSqueeze output", nullptr);
    m.playback = pw_stream_new(m.session.core, "LiveSqueeze output", outProps);

    if (m.capture == nullptr || m.playback == nullptr) {
        pw_thread_loop_unlock(m.session.loop);
        close();
        return Status::error("cannot create the PipeWire streams");
    }

    static const pw_stream_events captureEvents = {
        PW_VERSION_STREAM_EVENTS,
        nullptr,
        &Impl::streamState,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &Impl::captureProcess,
    };
    static const pw_stream_events playbackEvents = {
        PW_VERSION_STREAM_EVENTS,
        nullptr,
        &Impl::streamState,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &Impl::playbackProcess,
    };
    pw_stream_add_listener(m.capture, &m.captureHook, &captureEvents, &m);
    pw_stream_add_listener(m.playback, &m.playbackHook, &playbackEvents, &m);
    pw_thread_loop_unlock(m.session.loop);

    m.info = NegotiatedInfo{};
    m.info.captureRate = rate;
    m.info.playbackRate = rate;
    m.info.captureMap = layout;
    m.info.capturePeriodFrames = period;
    m.info.playbackPeriodFrames = period;
    m.info.captureLatencyMs = 1000.0 * static_cast<double>(period) / rate;
    m.info.playbackLatencyMs = 1000.0 * static_cast<double>(period) / rate;
    m.info.captureName = req.virtualSinkName + " (" + std::to_string(channels) + " ch)";
    m.info.playbackName = target->description;
    m.info.sharedClock = true;
    return Status::success();
}

Status PipeWireBackend::start() {
    Impl& m = *impl_;
    if (m.capture == nullptr || m.playback == nullptr) {
        return Status::error("devices are not open");
    }
    if (m.started) {
        return Status::success();
    }

    const int channels = m.captureChannels;
    const std::uint32_t rate = static_cast<std::uint32_t>(m.info.captureRate);

    std::uint8_t sinkBuf[1024];
    spa_pod_builder sinkBuilder = SPA_POD_BUILDER_INIT(sinkBuf, sizeof sinkBuf);
    spa_audio_info_raw sinkInfo{};
    sinkInfo.format = SPA_AUDIO_FORMAT_F32;
    sinkInfo.rate = rate;
    sinkInfo.channels = static_cast<std::uint32_t>(channels);
    for (int i = 0; i < channels; ++i) {
        sinkInfo.position[i] = toSpaChannel(m.info.captureMap.pos[i]);
    }
    const spa_pod* sinkParams[1];
    sinkParams[0] = spa_format_audio_raw_build(&sinkBuilder, SPA_PARAM_EnumFormat, &sinkInfo);

    std::uint8_t outBuf[1024];
    spa_pod_builder outBuilder = SPA_POD_BUILDER_INIT(outBuf, sizeof outBuf);
    spa_audio_info_raw outInfo{};
    outInfo.format = SPA_AUDIO_FORMAT_F32;
    outInfo.rate = rate;
    outInfo.channels = 2;
    outInfo.position[0] = SPA_AUDIO_CHANNEL_FL;
    outInfo.position[1] = SPA_AUDIO_CHANNEL_FR;
    const spa_pod* outParams[1];
    outParams[0] = spa_format_audio_raw_build(&outBuilder, SPA_PARAM_EnumFormat, &outInfo);

    m.running = true;
    pw_thread_loop_lock(m.session.loop);
    int res = pw_stream_connect(
        m.capture, PW_DIRECTION_INPUT, PW_ID_ANY,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
        sinkParams, 1);
    if (res >= 0) {
        res = pw_stream_connect(m.playback, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                                static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                                             PW_STREAM_FLAG_MAP_BUFFERS |
                                                             PW_STREAM_FLAG_RT_PROCESS),
                                outParams, 1);
    }
    pw_thread_loop_unlock(m.session.loop);
    if (res < 0) {
        m.running = false;
        return Status::error(std::string("cannot start the PipeWire streams: ") +
                             std::strerror(-res));
    }
    m.started = true;
    return Status::success();
}

void PipeWireBackend::stop() {
    Impl& m = *impl_;
    m.running = false;
    if (!m.started || m.session.loop == nullptr) {
        return;
    }
    // pw_stream_disconnect removes the node from the graph with a blocking call into the data
    // thread, so no process callback is running once it returns.
    pw_thread_loop_lock(m.session.loop);
    if (m.capture != nullptr) {
        pw_stream_disconnect(m.capture);
    }
    if (m.playback != nullptr) {
        pw_stream_disconnect(m.playback);
    }
    pw_thread_loop_unlock(m.session.loop);
    m.started = false;
}

void PipeWireBackend::close() {
    stop();
    Impl& m = *impl_;
    m.destroyStreams();
    if (m.sessionReady) {
        m.session.onEvent = nullptr;
        m.session.disconnect();
        m.session = Session{};
        m.sessionReady = false;
    }
}

NegotiatedInfo PipeWireBackend::info() const {
    return impl_->info;
}

} // namespace lsq
