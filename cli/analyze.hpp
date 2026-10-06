#pragma once

#include <vector>

namespace lsqcli {

struct ChannelStats {
    double samplePeakDb = -240.0;
    double truePeakDb = -240.0; // 16x oversampled estimate
    double rmsDb = -240.0;
};

struct LevelSpread {
    double p10 = -240.0; // dBFS, 10th percentile of the 400 ms RMS windows (ignoring near-silence)
    double p95 = -240.0; // 95th percentile
    double spread() const { return p95 - p10; }
};

// Per-channel statistics of interleaved audio.
std::vector<ChannelStats> analyzeChannels(const std::vector<float>& interleaved, int channels);

// Spread between loud and quiet passages of an interleaved signal (all channels combined):
// a rough, unweighted stand-in for loudness range, used to show what the compressor did.
LevelSpread levelSpread(const std::vector<float>& interleaved, int channels, double sampleRate);

} // namespace lsqcli
