#include "lsq/presets.hpp"

#include <algorithm>

namespace lsq {
namespace {

struct PresetInfo {
    Preset id;
    const char* key;
    const char* name;
};

constexpr PresetInfo kInfo[kPresetCount] = {
    {Preset::MovieNight, "movie-night", "Movie / Night"},
    {Preset::LateNight, "late-night", "Late night"},
    {Preset::DialogueBoost, "dialogue-boost", "Dialogue boost"},
    {Preset::GentleLeveler, "gentle-leveler", "Gentle leveler"},
    {Preset::LimiterOnly, "limiter-only", "Limiter only"},
};

const PresetInfo& info(Preset p) noexcept {
    const auto i = static_cast<std::size_t>(p);
    return kInfo[i < kPresetCount ? i : 0];
}

} // namespace

const char* presetName(Preset p) noexcept {
    return info(p).name;
}

const char* presetKey(Preset p) noexcept {
    return info(p).key;
}

bool presetFromKey(std::string_view key, Preset& out) noexcept {
    for (const PresetInfo& i : kInfo) {
        if (key == i.key) {
            out = i.id;
            return true;
        }
    }
    return false;
}

Params presetParams(Preset preset) noexcept {
    Params p; // the defaults are the Movie / Night preset
    switch (preset) {
    case Preset::MovieNight:
        break;
    case Preset::LateNight:
        p.thresholdDb = -30.0f;
        p.ratio = 6.0f;
        p.kneeDb = 6.0f;
        p.upThresholdDb = -38.0f;
        p.upRatio = 2.5f;
        p.maxBoostDb = 12.0f;
        p.noiseFloorDb = -62.0f;
        p.attackMs = 10.0f;
        p.releaseMs = 300.0f;
        p.makeupDb = 2.0f;
        p.ceilingDb = -3.0f;
        p.centerGainDb = 3.0f;
        p.surroundGainDb = -3.0f;
        break;
    case Preset::DialogueBoost:
        p.thresholdDb = -26.0f;
        p.ratio = 3.0f;
        p.kneeDb = 10.0f;
        p.upThresholdDb = -36.0f;
        p.upRatio = 2.0f;
        p.maxBoostDb = 9.0f;
        p.noiseFloorDb = -60.0f;
        p.attackMs = 10.0f;
        p.releaseMs = 300.0f;
        p.makeupDb = 2.0f;
        p.centerGainDb = 6.0f;
        p.surroundGainDb = -3.0f;
        break;
    case Preset::GentleLeveler:
        p.thresholdDb = -20.0f;
        p.ratio = 2.0f;
        p.kneeDb = 12.0f;
        p.upwardEnabled = false;
        p.attackMs = 20.0f;
        p.releaseMs = 600.0f;
        p.centerGainDb = 0.0f;
        p.surroundGainDb = 0.0f;
        break;
    case Preset::LimiterOnly:
        p.compEnabled = false;
        p.upwardEnabled = false;
        p.centerGainDb = 0.0f;
        p.surroundGainDb = 0.0f;
        break;
    }
    sanitize(p);
    return p;
}

Params blendParams(const Params& a, const Params& b, float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    Params out = a;
    for (std::size_t i = 0; i < paramCount(); ++i) {
        const ParamDesc& d = paramDesc(i);
        const float va = paramGet(a, d);
        const float vb = paramGet(b, d);
        if (d.kind == ParamKind::Bool) {
            paramSet(out, d, t < 0.5f ? va : vb);
        } else {
            paramSet(out, d, va + (vb - va) * t);
        }
    }
    sanitize(out);
    return out;
}

} // namespace lsq
