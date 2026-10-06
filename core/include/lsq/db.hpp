#pragma once

#include <cmath>

namespace lsq {

// Level reported for silence. Also the floor of every meter.
inline constexpr float kSilenceDb = -120.0f;

inline float dbToLin(float db) noexcept {
    return std::pow(10.0f, db * 0.05f);
}

inline double dbToLin(double db) noexcept {
    return std::pow(10.0, db * 0.05);
}

inline float linToDb(float lin) noexcept {
    return lin > 1e-6f ? 20.0f * std::log10(lin) : kSilenceDb;
}

} // namespace lsq
