#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "lsq/audio_backend.hpp"

namespace lsq {

enum class BackendKind {
    Auto,          // the best backend for this platform
    Miniaudio,     // WASAPI / CoreAudio / PulseAudio / ALSA through miniaudio
    MiniaudioNull, // miniaudio's software-timed null devices (tests)
    Fake,          // simulated devices that play a test tone (demos, GUI tests)
    PipeWire,      // native PipeWire virtual sink (Linux)
};

const char* backendKindName(BackendKind k) noexcept;
bool parseBackendKind(std::string_view name, BackendKind& out) noexcept;

// Whether this build contains the backend.
bool backendAvailable(BackendKind k) noexcept;

// Creates a backend, or returns null if it is not part of this build.
std::unique_ptr<IAudioBackend> createBackend(BackendKind kind);

} // namespace lsq
