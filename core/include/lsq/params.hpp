#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

namespace lsq {

// Every user-adjustable setting. Plain data: cheap to copy between threads.
// Gains and levels are in dB, times in milliseconds.
struct Params {
    // Downmix to stereo
    float centerGainDb = 3.0f;   // relative to the ITU -3 dB center coefficient (+3 dB = unity)
    float surroundGainDb = 0.0f; // relative to the ITU -3 dB surround coefficient
    float lfeGainDb = -6.0f;     // level of the LFE channel when enabled
    bool lfeEnabled = false;
    bool normalizeDownmix =
        false; // scale so that fully correlated surround content cannot exceed 1.0

    // Compressor
    bool compEnabled = true;
    float thresholdDb = -24.0f; // downward compression threshold (Td)
    float ratio = 4.0f;         // downward ratio (Rd), n:1
    float kneeDb = 8.0f;        // soft knee width, used for both thresholds
    bool upwardEnabled = true;
    float upThresholdDb = -34.0f; // below this level quiet sounds are boosted (Tu)
    float upRatio = 2.0f;         // upward ratio (Ru), n:1
    float maxBoostDb = 9.0f;      // cap on the upward boost (Bmax)
    float noiseFloorDb = -60.0f;  // no boost at or below this level (Nf)
    float attackMs = 15.0f;       // gain falling
    float releaseMs = 400.0f;     // gain rising
    float makeupDb = 0.0f;
    bool sidechainHpf = true; // 80 Hz high-pass on the detector so rumble does not drive it
    bool linkMax = false;     // detector uses max(L, R) instead of the power average

    // Limiter
    float ceilingDb = -1.0f; // true-peak ceiling (dBTP)
    float limiterReleaseMs = 100.0f;

    // Output
    float outTrimDb = 0.0f; // volume trim after the limiter (<= 0)
    bool bypass = false;    // downmix + safety clamp only
};

static_assert(std::is_standard_layout_v<Params>);
static_assert(std::is_trivially_copyable_v<Params>);

enum class ParamKind { Float, Bool };

// Describes one field of Params. This table is the single source of truth for GUI controls, the
// CLI `--set key=value` option and preset files.
struct ParamDesc {
    const char* key;   // stable identifier, e.g. "threshold_db"
    const char* label; // human readable name
    const char* unit;  // "dB", "ms", ":1" or ""
    const char* group; // "downmix", "compressor", "limiter" or "output"
    ParamKind kind;
    float minValue;
    float maxValue;
    float defaultValue;
    std::size_t offset; // offsetof(Params, field)
};

std::size_t paramCount() noexcept;
const ParamDesc& paramDesc(std::size_t index) noexcept;
const ParamDesc* findParam(std::string_view key) noexcept;

float paramGet(const Params& p, const ParamDesc& d) noexcept;
void paramSet(Params& p, const ParamDesc& d, float value) noexcept; // clamps to [min, max]

// Clamp everything into range, replace NaN with the default, and enforce Tu <= Td.
void sanitize(Params& p) noexcept;

// Parse "key=value" ("true"/"false"/"on"/"off" accepted for booleans) and apply it to `p`.
// Returns false and fills `error` when the key or value is not understood.
bool applyAssignment(Params& p, std::string_view assignment, std::string* error = nullptr);

} // namespace lsq
