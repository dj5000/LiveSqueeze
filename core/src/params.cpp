#include "lsq/params.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace lsq {
namespace {

#define LSQ_FLOAT(key, label, unit, group, lo, hi, field)                                          \
    { key, label, unit, group, ParamKind::Float, lo, hi, Params{}.field, offsetof(Params, field) }
#define LSQ_BOOL(key, label, group, field)                                                         \
    {                                                                                              \
        key, label, "", group, ParamKind::Bool, 0.0f, 1.0f, Params{}.field ? 1.0f : 0.0f,          \
            offsetof(Params, field)                                                                \
    }

const ParamDesc kTable[] = {
    LSQ_FLOAT("center_gain_db", "Center (dialogue) level", "dB", "downmix", -12.0f, 12.0f,
              centerGainDb),
    LSQ_FLOAT("surround_gain_db", "Surround level", "dB", "downmix", -12.0f, 12.0f, surroundGainDb),
    LSQ_FLOAT("lfe_gain_db", "Subwoofer (LFE) level", "dB", "downmix", -24.0f, 6.0f, lfeGainDb),
    LSQ_BOOL("lfe_enabled", "Include subwoofer", "downmix", lfeEnabled),
    LSQ_BOOL("normalize_downmix", "Normalize downmix", "downmix", normalizeDownmix),

    LSQ_BOOL("comp_enabled", "Compressor", "compressor", compEnabled),
    LSQ_FLOAT("threshold_db", "Threshold", "dB", "compressor", -60.0f, 0.0f, thresholdDb),
    LSQ_FLOAT("ratio", "Ratio", ":1", "compressor", 1.0f, 20.0f, ratio),
    LSQ_FLOAT("knee_db", "Knee", "dB", "compressor", 0.0f, 24.0f, kneeDb),
    LSQ_BOOL("upward_enabled", "Boost quiet sounds", "compressor", upwardEnabled),
    LSQ_FLOAT("up_threshold_db", "Boost below", "dB", "compressor", -70.0f, 0.0f, upThresholdDb),
    LSQ_FLOAT("up_ratio", "Boost ratio", ":1", "compressor", 1.0f, 10.0f, upRatio),
    LSQ_FLOAT("max_boost_db", "Maximum boost", "dB", "compressor", 0.0f, 24.0f, maxBoostDb),
    LSQ_FLOAT("noise_floor_db", "Noise floor", "dB", "compressor", -90.0f, -30.0f, noiseFloorDb),
    LSQ_FLOAT("attack_ms", "Attack", "ms", "compressor", 0.1f, 200.0f, attackMs),
    LSQ_FLOAT("release_ms", "Release", "ms", "compressor", 10.0f, 3000.0f, releaseMs),
    LSQ_FLOAT("makeup_db", "Makeup gain", "dB", "compressor", -12.0f, 24.0f, makeupDb),
    LSQ_BOOL("sidechain_hpf", "Ignore rumble in detector", "compressor", sidechainHpf),
    LSQ_BOOL("link_max", "Detector follows louder channel", "compressor", linkMax),

    LSQ_FLOAT("ceiling_db", "Ceiling", "dBTP", "limiter", -12.0f, 0.0f, ceilingDb),
    LSQ_FLOAT("limiter_release_ms", "Limiter release", "ms", "limiter", 10.0f, 1000.0f,
              limiterReleaseMs),

    LSQ_FLOAT("out_trim_db", "Output volume", "dB", "output", -60.0f, 0.0f, outTrimDb),
    LSQ_BOOL("bypass", "Bypass", "output", bypass),
};

#undef LSQ_FLOAT
#undef LSQ_BOOL

constexpr std::size_t kTableSize = sizeof(kTable) / sizeof(kTable[0]);

} // namespace

std::size_t paramCount() noexcept {
    return kTableSize;
}

const ParamDesc& paramDesc(std::size_t index) noexcept {
    return kTable[index < kTableSize ? index : 0];
}

const ParamDesc* findParam(std::string_view key) noexcept {
    for (const ParamDesc& d : kTable) {
        if (key == d.key) {
            return &d;
        }
    }
    return nullptr;
}

float paramGet(const Params& p, const ParamDesc& d) noexcept {
    const auto* base = reinterpret_cast<const unsigned char*>(&p) + d.offset;
    if (d.kind == ParamKind::Bool) {
        bool b = false;
        std::memcpy(&b, base, sizeof b);
        return b ? 1.0f : 0.0f;
    }
    float f = 0.0f;
    std::memcpy(&f, base, sizeof f);
    return f;
}

void paramSet(Params& p, const ParamDesc& d, float value) noexcept {
    if (std::isnan(value)) {
        value = d.defaultValue;
    }
    auto* base = reinterpret_cast<unsigned char*>(&p) + d.offset;
    if (d.kind == ParamKind::Bool) {
        const bool b = value >= 0.5f;
        std::memcpy(base, &b, sizeof b);
        return;
    }
    const float f = std::clamp(value, d.minValue, d.maxValue);
    std::memcpy(base, &f, sizeof f);
}

void sanitize(Params& p) noexcept {
    for (const ParamDesc& d : kTable) {
        paramSet(p, d, paramGet(p, d));
    }
    p.upThresholdDb = std::min(p.upThresholdDb, p.thresholdDb);
}

bool applyAssignment(Params& p, std::string_view assignment, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error != nullptr) {
            *error = msg;
        }
        return false;
    };

    const std::size_t eq = assignment.find('=');
    if (eq == std::string_view::npos) {
        return fail("expected key=value, got '" + std::string(assignment) + "'");
    }
    const std::string_view key = assignment.substr(0, eq);
    const std::string value(assignment.substr(eq + 1));

    const ParamDesc* d = findParam(key);
    if (d == nullptr) {
        return fail("unknown parameter '" + std::string(key) + "'");
    }

    float v = 0.0f;
    if (d->kind == ParamKind::Bool) {
        if (value == "1" || value == "true" || value == "on" || value == "yes") {
            v = 1.0f;
        } else if (value == "0" || value == "false" || value == "off" || value == "no") {
            v = 0.0f;
        } else {
            return fail("'" + std::string(key) + "' expects true/false, got '" + value + "'");
        }
    } else {
        char* end = nullptr;
        v = std::strtof(value.c_str(), &end);
        if (end == value.c_str() || *end != '\0' || !std::isfinite(v)) {
            return fail("'" + std::string(key) + "' expects a number, got '" + value + "'");
        }
    }
    paramSet(p, *d, v);
    p.upThresholdDb = std::min(p.upThresholdDb, p.thresholdDb);
    return true;
}

} // namespace lsq
