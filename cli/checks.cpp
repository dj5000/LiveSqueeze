#include "checks.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "lsq/downmix.hpp"
#include "lsq/params.hpp"

namespace lsqcli {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Left+right power, as a mono signal, in blocks of 1 ms: the shape of the signal over time.
std::vector<double> envelope(const Audio& a, std::size_t from, std::size_t to) {
    const std::size_t block = std::max<std::size_t>(1, a.sampleRate / 1000);
    const std::size_t n = static_cast<std::size_t>(a.map.n);
    std::vector<double> env;
    for (std::size_t f = from; f + block <= to; f += block) {
        double sum = 0.0;
        for (std::size_t i = f; i < f + block; ++i) {
            for (std::size_t c = 0; c < n; ++c) {
                const double v = static_cast<double>(a.samples[i * n + c]);
                sum += v * v;
            }
        }
        env.push_back(10.0 * std::log10(std::max(sum / static_cast<double>(block * n), 1e-14)));
    }
    return env;
}

double rmsDb(const Audio& a, std::size_t from, std::size_t to) {
    const std::size_t n = static_cast<std::size_t>(a.map.n);
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        for (std::size_t c = 0; c < n; ++c) {
            const double v = static_cast<double>(a.samples[i * n + c]);
            sum += v * v;
        }
    }
    const double count = static_cast<double>((to - from) * n);
    return 10.0 * std::log10(std::max(sum / std::max(count, 1.0), 1e-14));
}

// First and last frame where any channel exceeds -80 dBFS.
bool activeRange(const Audio& a, std::size_t& first, std::size_t& last) {
    const std::size_t n = static_cast<std::size_t>(a.map.n);
    const float threshold = 1e-4f;
    bool found = false;
    for (std::size_t f = 0; f < a.frames(); ++f) {
        for (std::size_t c = 0; c < n; ++c) {
            if (std::fabs(a.samples[f * n + c]) > threshold) {
                if (!found) {
                    first = f;
                    found = true;
                }
                last = f;
            }
        }
    }
    return found;
}

// Amplitude of the sine at `freq` in channel `ch` over [from, from + hann.size()), via a
// Hann-windowed single-bin DFT (so neighbouring tones leak negligibly).
double toneAmplitudeAt(const Audio& a, int ch, std::size_t from, const std::vector<double>& hann,
                       double hannSum, double freq) {
    const std::size_t n = static_cast<std::size_t>(a.map.n);
    const double w0 = 2.0 * kPi * freq / static_cast<double>(a.sampleRate);
    // Rotating phasor instead of sin/cos per sample: exact enough over a second in double.
    const double stepRe = std::cos(w0);
    const double stepIm = -std::sin(w0);
    double phRe = std::cos(w0 * static_cast<double>(from));
    double phIm = -std::sin(w0 * static_cast<double>(from));
    double re = 0.0;
    double im = 0.0;
    for (std::size_t k = 0; k < hann.size(); ++k) {
        const double v =
            static_cast<double>(a.samples[(from + k) * n + static_cast<std::size_t>(ch)]) * hann[k];
        re += v * phRe;
        im += v * phIm;
        const double nr = phRe * stepRe - phIm * stepIm;
        phIm = phRe * stepIm + phIm * stepRe;
        phRe = nr;
    }
    return 2.0 * std::sqrt(re * re + im * im) / hannSum;
}

// The amplitude of the tone near `freq` in one window. The engine nudges the resampling ratio by
// up to 2000 ppm to follow the device clocks (and sometimes needs all of it for a few seconds), so
// a 5 kHz tone can legitimately arrive 10 Hz off, many times the width of the window's main lobe:
// look for the strongest response within +/-2500 ppm.
double toneAmplitudeWindow(const Audio& a, int ch, std::size_t from, std::size_t len, double freq) {
    std::vector<double> hann(len);
    double hannSum = 0.0;
    for (std::size_t k = 0; k < len; ++k) {
        hann[k] =
            0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(k) / static_cast<double>(len));
        hannSum += hann[k];
    }
    const double binWidth = static_cast<double>(a.sampleRate) / static_cast<double>(len);
    const double span = 2.5e-3 * freq + 2.0 * binWidth;
    double bestFreq = freq;
    double best = 0.0;
    for (double f = freq - span; f <= freq + span; f += binWidth / 2.0) {
        const double v = toneAmplitudeAt(a, ch, from, hann, hannSum, f);
        if (v > best) {
            best = v;
            bestFreq = f;
        }
    }
    for (double f = bestFreq - binWidth / 2.0; f <= bestFreq + binWidth / 2.0;
         f += binWidth / 12.0) {
        best = std::max(best, toneAmplitudeAt(a, ch, from, hann, hannSum, f));
    }
    return best;
}

// The level of the tone over several one-second windows: the strongest one if the tone is
// supposed to be there, the weakest if it is supposed to be absent. A player or recorder that is
// scheduled late on a busy machine can drop or repeat a few milliseconds of audio. A jump in the
// waveform inside a window makes a tone look weaker and splatters a little energy across all
// frequencies, so it can only lower a reading that should be high and raise one that should be
// low, while a wrong gain or a wrong channel is wrong in every window.
double toneAmplitude(const Audio& a, int ch, std::size_t from, std::size_t to, double freq,
                     bool expectPresent) {
    const std::size_t len = static_cast<std::size_t>(a.sampleRate);
    double best = expectPresent ? 0.0 : 1e9;
    bool any = false;
    for (std::size_t f = from; f + len <= to; f += len / 2) {
        const double v = toneAmplitudeWindow(a, ch, f, len, freq);
        best = expectPresent ? std::max(best, v) : std::min(best, v);
        any = true;
    }
    return any ? best : 0.0;
}

// Short-term levels (dB) of consecutive overlapping windows over [from, to).
std::vector<double> shortTermLevels(const Audio& a, std::size_t from, std::size_t to) {
    const std::size_t win = static_cast<std::size_t>(0.4 * static_cast<double>(a.sampleRate));
    const std::size_t hop = static_cast<std::size_t>(0.1 * static_cast<double>(a.sampleRate));
    std::vector<double> levels;
    for (std::size_t f = from; f + win <= to; f += hop) {
        levels.push_back(rmsDb(a, f, f + win));
    }
    return levels;
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const double pos = p / 100.0 * static_cast<double>(v.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(pos);
    const std::size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (v[hi] - v[lo]) * (pos - static_cast<double>(lo));
}

double toDb(double lin) {
    return 20.0 * std::log10(std::max(lin, 1e-9));
}

} // namespace

CompareResult compareLevels(const Audio& ref, const Audio& test, double windowSeconds,
                            double maxDiffDb) {
    CompareResult r;
    std::size_t tFirst = 0;
    std::size_t tLast = 0;
    std::size_t rFirst = 0;
    std::size_t rLast = 0;
    if (!activeRange(test, tFirst, tLast) || !activeRange(ref, rFirst, rLast)) {
        r.report = "one of the files is silent\n";
        return r;
    }

    // Align on the shape of the loudness envelope: slide the reference over the recording.
    const std::vector<double> eRef =
        envelope(ref, rFirst, std::min<std::size_t>(rLast, rFirst + ref.sampleRate * 30ull));
    const std::vector<double> eTest = envelope(test, tFirst, tLast);
    const long maxLag = 1500; // +/- 1.5 s in ms
    long bestLag = 0;
    double bestScore = -1e300;
    const std::size_t span = std::min<std::size_t>(eRef.size(), 8000);
    for (long lag = -maxLag; lag <= maxLag; ++lag) {
        double score = 0.0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < span; ++i) {
            const long j = static_cast<long>(i) + lag;
            if (j < 0 || j >= static_cast<long>(eTest.size())) {
                continue;
            }
            const double d = eRef[i] - eTest[static_cast<std::size_t>(j)];
            score -= d * d;
            ++count;
        }
        if (count > span / 2) {
            score /= static_cast<double>(count);
            if (score > bestScore) {
                bestScore = score;
                bestLag = lag;
            }
        }
    }
    // Frame offset such that test[i + offset] corresponds to ref[i].
    const long offsetMs = bestLag;
    const double offsetFrames =
        static_cast<double>(tFirst) - static_cast<double>(rFirst) +
        static_cast<double>(offsetMs) * static_cast<double>(test.sampleRate) / 1000.0;

    char line[256];
    std::snprintf(
        line, sizeof line,
        "alignment: the recording starts %.1f ms after the reference (includes its lead-in)\n",
        1000.0 * offsetFrames / static_cast<double>(test.sampleRate));
    r.report += line;
    std::snprintf(line, sizeof line, "%-14s %12s %12s %8s\n", "window (s)", "reference",
                  "recording", "diff");
    r.report += line;

    const auto win = static_cast<std::size_t>(windowSeconds * static_cast<double>(ref.sampleRate));
    // A player or recorder on a busy machine can lose or add a few blocks of audio, so the
    // recording drifts against the reference by tens of milliseconds. A window next to a loud
    // transient would then change level just by moving its edges, which says nothing about the
    // processing: such windows (the reference's own level moves by more than half the allowed
    // difference when the window is shifted by 60 ms) are skipped.
    const auto guard = static_cast<std::ptrdiff_t>(0.06 * static_cast<double>(ref.sampleRate));
    auto sensitive = [&](std::size_t a0) {
        const double here = rmsDb(ref, a0, a0 + win);
        for (const std::ptrdiff_t shift : {-guard, -guard / 2, guard / 2, guard}) {
            const std::ptrdiff_t s0 = static_cast<std::ptrdiff_t>(a0) + shift;
            if (s0 < 0 || static_cast<std::size_t>(s0) + win > ref.frames()) {
                return true;
            }
            if (std::fabs(
                    rmsDb(ref, static_cast<std::size_t>(s0), static_cast<std::size_t>(s0) + win) -
                    here) > 0.5 * maxDiffDb) {
                return true;
            }
        }
        return false;
    };
    double worst = 0.0;
    int compared = 0;
    int skipped = 0;
    for (std::size_t a0 = ref.sampleRate * 2ull;
         a0 + win <= std::min<std::size_t>(ref.frames(), rLast); a0 += win) {
        const double b0 = static_cast<double>(a0) + offsetFrames;
        if (b0 < 0.0 || static_cast<std::size_t>(b0) + win > test.frames()) {
            continue;
        }
        const double refDb = rmsDb(ref, a0, a0 + win);
        if (refDb < -70.0) {
            continue;
        }
        if (sensitive(a0)) {
            ++skipped;
            std::snprintf(line, sizeof line,
                          "%5.1f - %-6.1f %9.2f dB   (skipped: next to a transient)\n",
                          static_cast<double>(a0) / ref.sampleRate,
                          static_cast<double>(a0 + win) / ref.sampleRate, refDb);
            r.report += line;
            continue;
        }
        const double testDb =
            rmsDb(test, static_cast<std::size_t>(b0), static_cast<std::size_t>(b0) + win);
        const double diff = testDb - refDb;
        worst = std::max(worst, std::fabs(diff));
        ++compared;
        std::snprintf(line, sizeof line, "%5.1f - %-6.1f %9.2f dB %9.2f dB %+7.2f\n",
                      static_cast<double>(a0) / ref.sampleRate,
                      static_cast<double>(a0 + win) / ref.sampleRate, refDb, testDb, diff);
        r.report += line;
    }
    bool ok = compared >= 3 && worst <= maxDiffDb;
    std::snprintf(line, sizeof line,
                  "%d windows compared (%d skipped), largest difference %.2f dB (allowed %.2f "
                  "dB): %s\n",
                  compared, skipped, worst, maxDiffDb,
                  compared >= 3 && worst <= maxDiffDb ? "PASS" : "FAIL");
    r.report += line;

    // The distribution of short-term levels does not depend on alignment, so it also covers the
    // loud and quiet passages next to transients. The 10th and 90th percentiles sit at the edge of
    // clusters of windows (silence, the loudest passages), where a few windows more or less move
    // them by a dB, so they are shown but not judged.
    const std::vector<double> lr = shortTermLevels(ref, rFirst, rLast);
    const std::vector<double> lt = shortTermLevels(test, tFirst, tLast);
    r.report += "short-term loudness (400 ms windows):\n";
    std::snprintf(line, sizeof line, "  %-12s %10s %10s %8s\n", "", "reference", "recording",
                  "diff");
    r.report += line;
    double worstPct = 0.0;
    auto row = [&](const char* name, double a, double b, bool judged) {
        if (judged) {
            worstPct = std::max(worstPct, std::fabs(b - a));
        }
        std::snprintf(line, sizeof line, "  %-12s %7.2f dB %7.2f dB %+7.2f%s\n", name, a, b, b - a,
                      judged ? "" : "  (not judged)");
        r.report += line;
    };
    row("whole file", rmsDb(ref, rFirst, rLast + 1), rmsDb(test, tFirst, tLast + 1), true);
    for (const double p : {10.0, 25.0, 50.0, 75.0, 90.0, 98.0}) {
        char name[16];
        std::snprintf(name, sizeof name, "percentile %.0f", p);
        row(name, percentile(lr, p), percentile(lt, p), p != 10.0 && p != 90.0);
    }
    const bool pctOk = !lr.empty() && !lt.empty() && worstPct <= maxDiffDb;
    std::snprintf(line, sizeof line,
                  "overall levels: largest difference %.2f dB (allowed %.2f dB): %s\n", worstPct,
                  maxDiffDb, pctOk ? "PASS" : "FAIL");
    r.report += line;
    ok = ok && pctOk;
    r.ok = ok;
    return r;
}

CompareResult checkTones(const lsq::ChannelMap& layout, const Audio& rec, double toleranceDb) {
    CompareResult r;
    if (rec.map.n != 2) {
        r.report = "the recording must be stereo\n";
        return r;
    }
    std::size_t first = 0;
    std::size_t last = 0;
    if (!activeRange(rec, first, last) ||
        last - first < static_cast<std::size_t>(rec.sampleRate) * 2) {
        r.report = "the recording is silent or too short\n";
        return r;
    }
    // Use the middle of the recording, away from the start-up and shut-down transients.
    const std::size_t len = last - first;
    const std::size_t from = first + len / 5;
    const std::size_t to = last - len / 5;

    // Expected: what the downmix does to the "tones" signal (channel c at 300 * 1.5^c Hz, -12
    // dBFS), for the limiter-only preset (center and surround at their ITU levels, no LFE).
    lsq::Params params;
    params.centerGainDb = 0.0f;
    params.surroundGainDb = 0.0f;
    float cl[lsq::kMaxChannels];
    float cr[lsq::kMaxChannels];
    lsq::Downmixer::computeCoefficients(layout, params, cl, cr);
    const double toneDb = -12.0;

    bool ok = true;
    char line[256];
    std::snprintf(line, sizeof line, "%-4s %9s | %12s %12s | %12s %12s\n", "ch", "freq",
                  "left want", "left got", "right want", "right got");
    r.report += line;
    for (int c = 0; c < layout.n; ++c) {
        const double freq = 300.0 * std::pow(1.5, c);
        const double wantL = cl[c] > 1e-6f ? toneDb + toDb(static_cast<double>(cl[c])) : -200.0;
        const double wantR = cr[c] > 1e-6f ? toneDb + toDb(static_cast<double>(cr[c])) : -200.0;
        const double gotL = toDb(toneAmplitude(rec, 0, from, to, freq, wantL > -100.0));
        const double gotR = toDb(toneAmplitude(rec, 1, from, to, freq, wantR > -100.0));
        // A channel that should be absent must be at least 40 dB below a -12 dB tone.
        auto good = [&](double want, double got) {
            return want < -100.0 ? got < toneDb - 40.0 : std::fabs(got - want) <= toleranceDb;
        };
        const bool chOk = good(wantL, gotL) && good(wantR, gotR);
        ok = ok && chOk;
        std::snprintf(line, sizeof line,
                      "%-4s %7.1f Hz | %9.2f dB %9.2f dB | %9.2f dB %9.2f dB  %s\n",
                      lsq::posName(layout.pos[c]), freq, wantL < -100.0 ? -99.0 : wantL, gotL,
                      wantR < -100.0 ? -99.0 : wantR, gotR, chOk ? "ok" : "MISMATCH");
        r.report += line;
    }
    r.ok = ok;
    r.report += ok ? "channel mapping: PASS\n" : "channel mapping: FAIL\n";
    return r;
}

} // namespace lsqcli
