#include "lsq/backend_factory.hpp"

#include "fake_backend.hpp"
#if defined(LSQ_HAVE_MINIAUDIO)
#include "ma_backend.hpp"
#endif

namespace lsq {

const char* backendKindName(BackendKind k) noexcept {
    switch (k) {
    case BackendKind::Auto:
        return "auto";
    case BackendKind::Miniaudio:
        return "miniaudio";
    case BackendKind::MiniaudioNull:
        return "null";
    case BackendKind::Fake:
        return "fake";
    case BackendKind::PipeWire:
        return "pipewire";
    }
    return "?";
}

bool parseBackendKind(std::string_view name, BackendKind& out) noexcept {
    for (BackendKind k : {BackendKind::Auto, BackendKind::Miniaudio, BackendKind::MiniaudioNull,
                          BackendKind::Fake, BackendKind::PipeWire}) {
        if (name == backendKindName(k)) {
            out = k;
            return true;
        }
    }
    return false;
}

bool backendAvailable(BackendKind k) noexcept {
    switch (k) {
    case BackendKind::Auto:
    case BackendKind::Fake:
        return true;
    case BackendKind::Miniaudio:
    case BackendKind::MiniaudioNull:
#if defined(LSQ_HAVE_MINIAUDIO)
        return true;
#else
        return false;
#endif
    case BackendKind::PipeWire:
        return false;
    }
    return false;
}

std::unique_ptr<IAudioBackend> createBackend(BackendKind kind) {
    if (kind == BackendKind::Auto) {
        kind = BackendKind::Miniaudio;
    }
    switch (kind) {
#if defined(LSQ_HAVE_MINIAUDIO)
    case BackendKind::Miniaudio:
        return std::make_unique<MiniaudioBackend>();
    case BackendKind::MiniaudioNull:
        return std::make_unique<MiniaudioBackend>(MiniaudioBackend::Options{true});
#endif
    case BackendKind::Fake:
        return std::make_unique<FakeBackend>();
    default:
        return nullptr;
    }
}

} // namespace lsq
