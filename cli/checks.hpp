#pragma once

#include <string>
#include <vector>

#include "wav_io.hpp"

namespace lsqcli {

struct CompareResult {
    bool ok = false;
    std::string report;
};

// Compares the loudness of `test` against `ref` window by window after aligning them (the test
// recording usually starts later, with silence around it, and is not sample-exact). Passes if
// every window louder than -70 dBFS agrees within `maxDiffDb`.
CompareResult compareLevels(const Audio& ref, const Audio& test, double windowSeconds,
                            double maxDiffDb);

// Checks channel mapping end to end. `recorded` is the stereo output obtained by playing the
// "tones" signal (one tone per input channel, see signals.hpp) through LiveSqueeze with the given
// downmix parameters; every tone must appear in the left/right output with the level the downmix
// coefficients predict.
CompareResult checkTones(const lsq::ChannelMap& inputLayout, const Audio& recorded,
                         double toleranceDb);

} // namespace lsqcli
