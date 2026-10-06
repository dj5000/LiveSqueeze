// lsq-run: headless live runner. Opens the audio devices, runs the engine and prints what it is
// doing. Useful on its own, for scripting, and to check a new machine before using the GUI.
//
//   lsq-run --list-devices [--json]
//   lsq-run --input "CABLE Output" --output "Speakers" [--preset late-night]
//   lsq-run --selftest [--json]       short run, prints a report to paste into a bug report

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "lsq/backend_factory.hpp"
#include "lsq/presets.hpp"
#include "lsq/supervisor.hpp"
#include "lsq/version.hpp"

using namespace lsq;

namespace {

std::atomic<bool> g_stop{false};
void onSignal(int) {
    g_stop = true;
}

struct Options {
    std::string backend = "auto";
    std::string input;
    std::string output;
    std::string preset;
    std::vector<std::string> sets;
    std::string latency = "balanced";
    std::string inputLayout;
    double duration = 0.0;
    bool listDevices = false;
    bool json = false;
    bool selftest = false;
    bool quiet = false;
    bool help = false;
};

[[noreturn]] void die(const std::string& msg) {
    std::fprintf(stderr, "lsq-run: %s\n", msg.c_str());
    std::exit(2);
}

void usage() {
    std::printf(
        "LiveSqueeze %s - headless runner\n\n"
        "  lsq-run --list-devices [--json]\n"
        "  lsq-run [--input DEVICE] [--output DEVICE] [--preset NAME] [--set key=value]...\n"
        "          [--latency low|balanced|safe] [--input-layout 5.1|7.1|...] [--duration "
        "SECONDS]\n"
        "  lsq-run --selftest [--json]\n\n"
        "  --backend auto|miniaudio|null|fake   (default auto)\n"
        "  DEVICE is an id from --list-devices or part of a device name.\n"
        "  Without --input the first virtual cable is used; without --output the default "
        "speakers.\n"
        "  Ctrl-C stops a run.\n",
        versionString());
}

Options parse(int argc, char** argv) {
    Options o;
    auto value = [&](int& i) -> std::string {
        if (i + 1 >= argc) {
            die(std::string("missing value for ") + argv[i]);
        }
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--backend")
            o.backend = value(i);
        else if (s == "--input")
            o.input = value(i);
        else if (s == "--output")
            o.output = value(i);
        else if (s == "--preset")
            o.preset = value(i);
        else if (s == "--set")
            o.sets.push_back(value(i));
        else if (s == "--latency")
            o.latency = value(i);
        else if (s == "--input-layout")
            o.inputLayout = value(i);
        else if (s == "--duration")
            o.duration = std::atof(value(i).c_str());
        else if (s == "--list-devices")
            o.listDevices = true;
        else if (s == "--json")
            o.json = true;
        else if (s == "--selftest")
            o.selftest = true;
        else if (s == "--quiet")
            o.quiet = true;
        else if (s == "-h" || s == "--help")
            o.help = true;
        else
            die("unknown option '" + s + "'");
    }
    return o;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

// Resolves "--input X": an exact id, or a unique case-insensitive substring of a name.
std::string resolveDevice(const std::vector<DeviceInfo>& devices, const std::string& what,
                          const char* label) {
    if (what.empty()) {
        return {};
    }
    for (const DeviceInfo& d : devices) {
        if (d.id == what) {
            return d.id;
        }
    }
    std::vector<const DeviceInfo*> hits;
    for (const DeviceInfo& d : devices) {
        if (lower(d.name).find(lower(what)) != std::string::npos) {
            hits.push_back(&d);
        }
    }
    if (hits.size() == 1) {
        return hits[0]->id;
    }
    if (hits.empty()) {
        die(std::string("no ") + label + " device matches '" + what + "' (see --list-devices)");
    }
    std::string names;
    for (const DeviceInfo* d : hits) {
        names += "\n    " + d->name;
    }
    die(std::string("'") + what + "' matches several " + label + " devices:" + names);
}

void listDevices(Supervisor& sup, bool json) {
    const Dir dirs[] = {Dir::Capture, Dir::Playback};
    bool first = true;
    if (json) {
        std::printf("[\n");
    }
    for (Dir dir : dirs) {
        for (const DeviceInfo& d : sup.listDevices(dir)) {
            const char* kind = dir == Dir::Capture ? "input" : "output";
            if (json) {
                std::printf("%s  {\"direction\": \"%s\", \"id\": \"%s\", \"name\": \"%s\", "
                            "\"channels\": %d, \"default\": %s, \"virtual\": %s}",
                            first ? "" : ",\n", kind, jsonEscape(d.id).c_str(),
                            jsonEscape(d.name).c_str(), d.channels, d.isDefault ? "true" : "false",
                            d.looksVirtual ? "true" : "false");
                first = false;
            } else {
                std::printf("%-7s %s%s%s\n          id: %s  channels: %d\n", kind, d.name.c_str(),
                            d.isDefault ? "  [default]" : "", d.looksVirtual ? "  [virtual]" : "",
                            d.id.c_str(), d.channels);
            }
        }
    }
    if (json) {
        std::printf("\n]\n");
    }
}

void printSelftest(Supervisor& sup, const Options& o, bool pass, const char* backend) {
    const NegotiatedInfo n = sup.negotiated();
    const EngineStats s = sup.stats();
    if (o.json) {
        std::printf(
            "{\n  \"version\": \"%s\",\n  \"backend\": \"%s\",\n  \"pass\": %s,\n"
            "  \"state\": \"%s\",\n  \"status\": \"%s\",\n  \"error\": \"%s\",\n"
            "  \"input\": {\"name\": \"%s\", \"rate\": %.0f, \"channels\": %d, \"layout\": \"%s\", "
            "\"period_frames\": %u},\n"
            "  \"output\": {\"name\": \"%s\", \"rate\": %.0f, \"period_frames\": %u},\n"
            "  \"engine\": {\"capture_callbacks\": %llu, \"playback_callbacks\": %llu, "
            "\"underruns\": %llu, \"overruns\": %llu, \"overfill_skips\": %llu, \"primes\": %llu, "
            "\"fill_ms\": %.2f, \"target_fill_ms\": %.2f, \"trim_ppm\": %.1f, "
            "\"added_latency_ms\": %.1f}\n}\n",
            versionString(), backend, pass ? "true" : "false", stateName(sup.state()),
            jsonEscape(sup.statusText()).c_str(), jsonEscape(sup.lastError()).c_str(),
            jsonEscape(n.captureName).c_str(), n.captureRate, n.captureMap.n,
            jsonEscape(n.captureMap.toString()).c_str(), n.capturePeriodFrames,
            jsonEscape(n.playbackName).c_str(), n.playbackRate, n.playbackPeriodFrames,
            static_cast<unsigned long long>(s.captureCallbacks),
            static_cast<unsigned long long>(s.playbackCallbacks),
            static_cast<unsigned long long>(s.underruns),
            static_cast<unsigned long long>(s.overruns),
            static_cast<unsigned long long>(s.overfillSkips),
            static_cast<unsigned long long>(s.primes), s.fillMs, s.targetFillMs, s.trimPpm,
            s.latencyMs);
        return;
    }
    std::printf("self-test %s\n", pass ? "PASSED" : "FAILED");
    std::printf("  backend:  %s\n  state:    %s (%s)\n", backend, stateName(sup.state()),
                sup.statusText().c_str());
    if (!sup.lastError().empty()) {
        std::printf("  error:    %s\n", sup.lastError().c_str());
    }
    std::printf("  input:    %s, %.0f Hz, %d channels (%s), period %u\n", n.captureName.c_str(),
                n.captureRate, n.captureMap.n, n.captureMap.toString().c_str(),
                n.capturePeriodFrames);
    std::printf("  output:   %s, %.0f Hz, period %u\n", n.playbackName.c_str(), n.playbackRate,
                n.playbackPeriodFrames);
    std::printf("  callbacks: capture %llu, playback %llu; underruns %llu, overruns %llu, "
                "start-ups %llu\n",
                static_cast<unsigned long long>(s.captureCallbacks),
                static_cast<unsigned long long>(s.playbackCallbacks),
                static_cast<unsigned long long>(s.underruns),
                static_cast<unsigned long long>(s.overruns),
                static_cast<unsigned long long>(s.primes));
    std::printf(
        "  queue:    %.1f ms (target %.1f), clock correction %+.1f ppm, added latency %.1f ms\n",
        s.fillMs, s.targetFillMs, s.trimPpm, s.latencyMs);
}

} // namespace

int main(int argc, char** argv) {
    const Options o = parse(argc, argv);
    if (o.help) {
        usage();
        return 0;
    }

    BackendKind kind;
    if (!parseBackendKind(o.backend, kind)) {
        die("unknown backend '" + o.backend + "' (auto, miniaudio, null, fake)");
    }
    if (!backendAvailable(kind)) {
        die("backend '" + o.backend + "' is not part of this build");
    }
    BackendFactory factory = [kind] { return createBackend(kind); };

    Params params;
    if (!o.preset.empty()) {
        Preset p;
        if (!presetFromKey(o.preset, p)) {
            die("unknown preset '" + o.preset + "'");
        }
        params = presetParams(p);
    }
    for (const std::string& s : o.sets) {
        std::string err;
        if (!applyAssignment(params, s, &err)) {
            die(err);
        }
    }
    ParamStore store;
    store.publish(params);

    Supervisor sup(factory, &store);
    if (o.listDevices) {
        listDevices(sup, o.json);
        return 0;
    }

    SupervisorConfig cfg;
    cfg.request.captureId = resolveDevice(sup.listDevices(Dir::Capture), o.input, "input");
    cfg.request.playbackId = resolveDevice(sup.listDevices(Dir::Playback), o.output, "output");
    if (o.latency == "low")
        cfg.latency = LatencyMode::Low;
    else if (o.latency == "safe")
        cfg.latency = LatencyMode::Safe;
    else if (o.latency != "balanced")
        die("--latency must be low, balanced or safe");
    if (!o.inputLayout.empty() && !ChannelMap::parse(o.inputLayout, cfg.captureLayout)) {
        die("unknown --input-layout '" + o.inputLayout + "'");
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    sup.start(cfg);
    const double seconds = o.selftest && o.duration <= 0.0 ? 3.0 : o.duration;
    const auto t0 = std::chrono::steady_clock::now();
    auto lastPrint = t0;
    MeterFrame m;
    float inPeak = -120.0f;
    float outPeak = -120.0f;
    float gr = 0.0f;

    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        while (sup.popMeter(m)) {
            inPeak = std::max(inPeak, m.inPeakDb);
            outPeak = std::max(outPeak, m.outPeakDb);
            gr = std::min(gr, m.compGainDb + m.limiterGainDb);
        }
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - t0).count();
        if (seconds > 0.0 && elapsed >= seconds) {
            break;
        }
        if (!o.quiet && !o.selftest && now - lastPrint >= std::chrono::seconds(1)) {
            lastPrint = now;
            const EngineStats s = sup.stats();
            std::printf(
                "[%s] %s | queue %.1f ms, drift %+.1f ppm, underruns %llu | in %.1f out %.1f dBFS, "
                "reduction %.1f dB\n",
                stateName(sup.state()), sup.statusText().c_str(), s.fillMs, s.trimPpm,
                static_cast<unsigned long long>(s.underruns), static_cast<double>(inPeak),
                static_cast<double>(outPeak), static_cast<double>(gr));
            std::fflush(stdout);
            inPeak = outPeak = -120.0f;
            gr = 0.0f;
        }
    }

    int rc = 0;
    if (o.selftest) {
        const EngineStats s = sup.stats();
        const bool pass = sup.state() == SupervisorState::Running && s.captureCallbacks > 0 &&
                          s.playbackCallbacks > 0 && s.underruns <= 1 && s.overruns == 0;
        printSelftest(sup, o, pass, o.backend.c_str());
        rc = pass ? 0 : 1;
    }
    sup.stop();
    return rc;
}
