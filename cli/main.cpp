// lsq-cli: offline tool built on the same DSP core as the live application.
//
//   lsq-cli gen      generate a test signal as a WAV file
//   lsq-cli analyze  peaks, true peaks and levels of a WAV file
//   lsq-cli process  downmix + compress + limit a WAV file
//   lsq-cli curve    print the static compression curve as CSV
//   lsq-cli bench    measure how much faster than real time the DSP runs
//   lsq-cli presets  list the presets
//   lsq-cli params   list every adjustable parameter

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "analyze.hpp"
#include "lsq/downmix.hpp"
#include "lsq/dynamics_chain.hpp"
#include "lsq/gain_computer.hpp"
#include "lsq/presets.hpp"
#include "lsq/version.hpp"
#include "signals.hpp"
#include "wav_io.hpp"

namespace {

struct Args {
    std::vector<std::string> positional;
    std::vector<std::string> sets; // --set key=value
    std::string preset;
    std::string layout;
    std::string kind = "movie";
    std::string out;
    double seconds = 20.0;
    double rate = 48000.0;
    double from = -90.0;
    double to = 0.0;
    double step = 1.0;
    bool compensate = true;
    bool help = false;
};

[[noreturn]] void die(const std::string& msg) {
    std::fprintf(stderr, "lsq-cli: %s\n", msg.c_str());
    std::exit(2);
}

Args parseArgs(int argc, char** argv, int first) {
    Args a;
    auto value = [&](int& i) -> std::string {
        if (i + 1 >= argc) {
            die(std::string("missing value for ") + argv[i]);
        }
        return argv[++i];
    };
    auto number = [&](int& i) -> double {
        const std::string opt = argv[i];
        const std::string v = value(i);
        char* end = nullptr;
        const double d = std::strtod(v.c_str(), &end);
        if (end == v.c_str() || *end != '\0') {
            die("expected a number after " + opt + ", got '" + v + "'");
        }
        return d;
    };
    for (int i = first; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--preset")
            a.preset = value(i);
        else if (s == "--set")
            a.sets.push_back(value(i));
        else if (s == "--layout")
            a.layout = value(i);
        else if (s == "--kind")
            a.kind = value(i);
        else if (s == "--out")
            a.out = value(i);
        else if (s == "--seconds")
            a.seconds = number(i);
        else if (s == "--rate")
            a.rate = number(i);
        else if (s == "--from")
            a.from = number(i);
        else if (s == "--to")
            a.to = number(i);
        else if (s == "--step")
            a.step = number(i);
        else if (s == "--no-compensate")
            a.compensate = false;
        else if (s == "-h" || s == "--help")
            a.help = true;
        else if (!s.empty() && s[0] == '-' && s.size() > 1)
            die("unknown option '" + s + "'");
        else
            a.positional.push_back(s);
    }
    return a;
}

lsq::Params buildParams(const Args& a) {
    lsq::Params p;
    if (!a.preset.empty()) {
        lsq::Preset preset;
        if (!lsq::presetFromKey(a.preset, preset)) {
            die("unknown preset '" + a.preset + "' (see 'lsq-cli presets')");
        }
        p = lsq::presetParams(preset);
    }
    for (const std::string& s : a.sets) {
        std::string err;
        if (!lsq::applyAssignment(p, s, &err)) {
            die(err);
        }
    }
    return p;
}

void usage() {
    std::printf(
        "LiveSqueeze %s - offline tool\n\n"
        "usage:\n"
        "  lsq-cli gen --kind movie|sine|noise|tones --layout 5.1 --seconds 20 --rate 48000 --out "
        "in.wav\n"
        "  lsq-cli analyze file.wav\n"
        "  lsq-cli process in.wav out.wav [--preset movie-night] [--set key=value]... [--layout "
        "5.1]\n"
        "                                 [--no-compensate]\n"
        "  lsq-cli curve [--preset name] [--set key=value]... [--from -90 --to 0 --step 1]\n"
        "  lsq-cli bench [--layout 7.1] [--seconds 60] [--rate 48000] [--preset name]\n"
        "  lsq-cli presets\n"
        "  lsq-cli params\n",
        lsq::versionString());
}

int cmdGen(const Args& a) {
    lsq::ChannelMap map = lsq::ChannelMap::standard(lsq::Layout::Surround51);
    if (!a.layout.empty() && !lsq::ChannelMap::parse(a.layout, map)) {
        die("unknown layout '" + a.layout + "' (mono, stereo, 2.1, quad, 5.1, 7.1)");
    }
    if (a.out.empty()) {
        die("gen needs --out file.wav");
    }
    lsqcli::Audio audio;
    std::string err;
    if (!lsqcli::generateSignal(a.kind, map, static_cast<std::uint32_t>(a.rate), a.seconds, audio,
                                err) ||
        !lsqcli::writeWav(a.out, audio, err)) {
        die(err);
    }
    std::printf("wrote %s: %s, %s, %.1f s at %.0f Hz\n", a.out.c_str(), a.kind.c_str(),
                map.toString().c_str(), a.seconds, a.rate);
    return 0;
}

void printStats(const char* title, const std::vector<float>& x, const lsq::ChannelMap& map,
                double sr) {
    const auto stats = lsqcli::analyzeChannels(x, map.n);
    std::printf("%s\n", title);
    for (int c = 0; c < map.n; ++c) {
        const auto& s = stats[static_cast<std::size_t>(c)];
        std::printf("  ch%d %-3s  sample peak %7.2f dBFS   true peak %7.2f dBTP   rms %7.2f dBFS\n",
                    c, lsq::posName(map.pos[c]), s.samplePeakDb, s.truePeakDb, s.rmsDb);
    }
    const auto spread = lsqcli::levelSpread(x, map.n, sr);
    std::printf("  level spread (400 ms windows): quiet(p10) %.1f  loud(p95) %.1f  range %.1f dB\n",
                spread.p10, spread.p95, spread.spread());
}

int cmdAnalyze(const Args& a) {
    if (a.positional.size() != 1) {
        die("analyze needs one WAV file");
    }
    lsqcli::Audio audio;
    std::string err;
    if (!lsqcli::readWav(a.positional[0], audio, err)) {
        die(err);
    }
    std::printf("%s: %d channels (%s), %u Hz, %.2f s\n", a.positional[0].c_str(), audio.map.n,
                audio.map.toString().c_str(), audio.sampleRate,
                static_cast<double>(audio.frames()) / audio.sampleRate);
    printStats("levels", audio.samples, audio.map, audio.sampleRate);
    return 0;
}

int cmdProcess(const Args& a) {
    if (a.positional.size() != 2) {
        die("process needs an input and an output WAV file");
    }
    lsqcli::Audio in;
    std::string err;
    if (!lsqcli::readWav(a.positional[0], in, err)) {
        die(err);
    }
    if (!a.layout.empty()) {
        lsq::ChannelMap forced;
        if (!lsq::ChannelMap::parse(a.layout, forced) || forced.n != in.map.n) {
            die("--layout '" + a.layout + "' does not match the file's " +
                std::to_string(in.map.n) + " channels");
        }
        in.map = forced;
    }
    const lsq::Params params = buildParams(a);
    const double sr = static_cast<double>(in.sampleRate);
    const std::size_t frames = in.frames();

    lsq::Downmixer downmix;
    downmix.prepare(in.map, sr);
    lsq::DynamicsChain chain;
    chain.prepare(sr);
    const std::size_t latency = static_cast<std::size_t>(chain.latencyFrames());

    // Downmix, then flush the chain's delay with silence so the output lines up with the input.
    std::vector<float> stereo(2 * (frames + latency), 0.0f);
    downmix.process(in.samples.data(), stereo.data(), frames, params);
    std::vector<float> reference(stereo.begin(),
                                 stereo.begin() + static_cast<std::ptrdiff_t>(2 * frames));

    constexpr std::size_t kBlock = 480;
    for (std::size_t f = 0; f < frames + latency; f += kBlock) {
        chain.process(stereo.data() + 2 * f, std::min(kBlock, frames + latency - f), params);
    }

    lsqcli::Audio out;
    out.sampleRate = in.sampleRate;
    out.map = lsq::ChannelMap::standard(lsq::Layout::Stereo);
    const std::size_t skip = a.compensate ? latency : 0;
    out.samples.assign(stereo.begin() + static_cast<std::ptrdiff_t>(2 * skip),
                       stereo.begin() + static_cast<std::ptrdiff_t>(2 * (skip + frames)));
    if (!lsqcli::writeWav(a.positional[1], out, err)) {
        die(err);
    }

    std::printf("processed %s -> %s (%s -> stereo, latency %zu frames = %.2f ms%s)\n",
                a.positional[0].c_str(), a.positional[1].c_str(), in.map.toString().c_str(),
                latency, 1000.0 * static_cast<double>(latency) / sr,
                a.compensate ? ", compensated" : "");
    printStats("downmix only (before compression)", reference, out.map, sr);
    printStats("after compression and limiting", out.samples, out.map, sr);
    return 0;
}

int cmdCurve(const Args& a) {
    const lsq::Params p = buildParams(a);
    if (a.step <= 0.0) {
        die("--step must be positive");
    }
    std::printf("input_db,gain_db,output_db\n");
    for (double level = a.from; level <= a.to + 1e-9; level += a.step) {
        const float g = lsq::curve::staticGainDb(static_cast<float>(level), p);
        std::printf("%.2f,%.3f,%.3f\n", level, static_cast<double>(g),
                    level + static_cast<double>(g));
    }
    return 0;
}

int cmdBench(const Args& a) {
    lsq::ChannelMap map = lsq::ChannelMap::standard(lsq::Layout::Surround71);
    if (!a.layout.empty() && !lsq::ChannelMap::parse(a.layout, map)) {
        die("unknown layout '" + a.layout + "'");
    }
    const lsq::Params params = buildParams(a);
    lsqcli::Audio audio;
    std::string err;
    if (!lsqcli::generateSignal("movie", map, static_cast<std::uint32_t>(a.rate),
                                std::min(a.seconds, 20.0), audio, err)) {
        die(err);
    }
    lsq::Downmixer downmix;
    downmix.prepare(map, a.rate);
    lsq::DynamicsChain chain;
    chain.prepare(a.rate);

    constexpr std::size_t kBlock = 480;
    std::vector<float> stereo(2 * kBlock);
    const std::size_t frames = audio.frames();
    const std::size_t total = static_cast<std::size_t>(a.seconds * a.rate);

    const auto t0 = std::chrono::steady_clock::now();
    std::size_t done = 0;
    double sink = 0.0;
    while (done < total) {
        for (std::size_t f = 0; f + kBlock <= frames && done < total; f += kBlock, done += kBlock) {
            downmix.process(audio.samples.data() + f * static_cast<std::size_t>(map.n),
                            stereo.data(), kBlock, params);
            chain.process(stereo.data(), kBlock, params);
            sink += static_cast<double>(stereo[0]);
        }
    }
    const double wall =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double audioSeconds = static_cast<double>(done) / a.rate;
    std::printf(
        "%s at %.0f Hz: %.1f s of audio in %.3f s  =  %.0fx real time  (%.2f%% of one core)%s\n",
        map.toString().c_str(), a.rate, audioSeconds, wall, audioSeconds / wall,
        100.0 * wall / audioSeconds, sink == 12345.678 ? "!" : "");
    return 0;
}

int cmdPresets() {
    for (std::size_t i = 0; i < lsq::kPresetCount; ++i) {
        const auto p = static_cast<lsq::Preset>(i);
        std::printf("%-16s %s\n", lsq::presetKey(p), lsq::presetName(p));
    }
    return 0;
}

int cmdParams() {
    const lsq::Params defaults;
    for (std::size_t i = 0; i < lsq::paramCount(); ++i) {
        const lsq::ParamDesc& d = lsq::paramDesc(i);
        if (d.kind == lsq::ParamKind::Bool) {
            std::printf("%-20s %-10s bool          default %-6s  %s\n", d.key, d.group,
                        lsq::paramGet(defaults, d) > 0.5f ? "true" : "false", d.label);
        } else {
            std::printf("%-20s %-10s %7.1f..%-7.1f default %-6.1f %s  %s\n", d.key, d.group,
                        static_cast<double>(d.minValue), static_cast<double>(d.maxValue),
                        static_cast<double>(d.defaultValue), d.unit, d.label);
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    const std::string cmd = argv[1];
    if (cmd == "-h" || cmd == "--help" || cmd == "help") {
        usage();
        return 0;
    }
    if (cmd == "--version" || cmd == "version") {
        std::printf("%s\n", lsq::versionString());
        return 0;
    }
    const Args a = parseArgs(argc, argv, 2);
    if (a.help) {
        usage();
        return 0;
    }
    if (cmd == "gen")
        return cmdGen(a);
    if (cmd == "analyze")
        return cmdAnalyze(a);
    if (cmd == "process")
        return cmdProcess(a);
    if (cmd == "curve")
        return cmdCurve(a);
    if (cmd == "bench")
        return cmdBench(a);
    if (cmd == "presets")
        return cmdPresets();
    if (cmd == "params")
        return cmdParams();
    std::fprintf(stderr, "lsq-cli: unknown command '%s'\n\n", cmd.c_str());
    usage();
    return 2;
}
