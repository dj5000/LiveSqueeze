#pragma once

#include <cmath>

namespace lsq {

// Second-order IIR section in transposed direct form II, with double precision state.
struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    // RBJ cookbook high-pass.
    static Biquad highpass(double sampleRate, double f0, double q = 0.70710678118654752) {
        const double pi = 3.14159265358979323846;
        const double w0 = 2.0 * pi * f0 / sampleRate;
        const double cw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        Biquad b;
        b.b0 = (1.0 + cw) * 0.5 / a0;
        b.b1 = -(1.0 + cw) / a0;
        b.b2 = (1.0 + cw) * 0.5 / a0;
        b.a1 = -2.0 * cw / a0;
        b.a2 = (1.0 - alpha) / a0;
        return b;
    }

    double process(double x) noexcept {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0.0; }
};

} // namespace lsq
