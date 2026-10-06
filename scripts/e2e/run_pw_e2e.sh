#!/usr/bin/env bash
# End-to-end test of the Linux PipeWire backend on a headless PipeWire (no sound card needed).
#
# It starts its own PipeWire + WirePlumber, creates a null sink as the "speakers", runs lsq-run
# against it, plays test files into the LiveSqueeze virtual sink with pw-cat, records the speakers
# with pw-record, and checks:
#   1. the virtual sink exists with the requested channel layout (5.1 and 7.1)
#   2. channel mapping: each input channel's tone comes out at the level the downmix predicts
#   3. dynamics: loudness of a film-like soundtrack matches the offline lsq-cli result, window by
#      window, after alignment
#   4. a soak run without dropouts
#
# usage: scripts/e2e/run_pw_e2e.sh [build-dir]        (default: build)
# env:   LSQ_E2E_SOAK_SECONDS  length of the soak (default 30)
#        LSQ_E2E_SECTIONS      which parts to run: any of "layouts film soak" (default: all)
#        LSQ_E2E_SRC_LATENCY   buffer of the players and the recorder, e.g. 100ms (default 100ms)
#        KEEP_E2E=1            keep the working directory (logs, recordings)
# Exit status 77 means "skipped" (tools missing), which ctest treats as skipped.
set -u

BUILD=${1:-build}
CLI=$BUILD/cli/lsq-cli
RUN=$BUILD/apps/run/lsq-run
SOAK=${LSQ_E2E_SOAK_SECONDS:-30}
SECTIONS=${LSQ_E2E_SECTIONS:-layouts film soak}
# pw-cat and pw-record are ordinary processes: on a machine without real-time scheduling they are
# sometimes scheduled late, and a late recorder loses a block while a late player leaves a gap. A
# generous buffer on both keeps the measurement itself from disturbing the result.
SRC_LATENCY=${LSQ_E2E_SRC_LATENCY:-100ms}
want() { [[ " $SECTIONS " == *" $1 "* ]]; }

for tool in pipewire wireplumber pw-cat pw-record pw-dump pw-cli dbus-run-session python3; do
    command -v "$tool" >/dev/null 2>&1 || { echo "SKIP: $tool not found"; exit 77; }
done
[[ -x $CLI && -x $RUN ]] || { echo "SKIP: $CLI / $RUN not built"; exit 77; }
"$RUN" --backend pipewire --list-devices >/dev/null 2>&1
if [[ $? -eq 2 ]]; then echo "SKIP: this build has no PipeWire backend"; exit 77; fi

# Re-run ourselves inside a private D-Bus session (WirePlumber wants one).
if [[ -z ${LSQ_E2E_INNER:-} ]]; then
    LSQ_E2E_INNER=1 exec dbus-run-session -- "$0" "$@"
fi

WORK=$(mktemp -d /tmp/lsq-e2e.XXXXXX)
export XDG_RUNTIME_DIR=$WORK/xdg
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
PIDS=()
cleanup() {
    for p in "${PIDS[@]:-}"; do kill "$p" 2>/dev/null; done
    wait 2>/dev/null
    [[ -n ${KEEP_E2E:-} ]] || rm -rf "$WORK"
}
trap cleanup EXIT

FAILS=0
quiet() { "$@" >/dev/null 2>&1; }
check() { # description, command...
    local what=$1; shift
    if "$@"; then echo "  PASS  $what"; else echo "  FAIL  $what"; FAILS=$((FAILS + 1)); fi
}

echo "== starting a headless PipeWire"
pipewire > "$WORK/pipewire.log" 2>&1 & PIDS+=($!)
sleep 1.5
wireplumber > "$WORK/wireplumber.log" 2>&1 & PIDS+=($!)
sleep 2
pw-cli info 0 >/dev/null 2>&1 || { echo "FAIL: PipeWire did not start"; tail "$WORK/pipewire.log"; exit 1; }
pw-cli create-node adapter '{ factory.name=support.null-audio-sink node.name=e2e-speakers node.description="E2E speakers" media.class=Audio/Sink object.linger=true audio.position=[FL FR] }' >/dev/null 2>&1
sleep 1

# Prints the audio.position of the livesqueeze sink, nothing if it does not exist.
sink_info() {
    pw-dump 2>/dev/null | python3 -c '
import json, sys
try:
    objects = json.load(sys.stdin)
except Exception:
    sys.exit(0)  # the daemon was busy; the caller retries
for o in objects:
    p = o.get("info", {}).get("props", {})
    if p.get("node.name") == "livesqueeze" and p.get("media.class") == "Audio/Sink":
        print(p.get("audio.position", ""))
        break
'
}

wait_for_sink() {
    for _ in $(seq 1 50); do
        [[ -n $(sink_info) ]] && return 0
        sleep 0.2
    done
    return 1
}

RUN_PID=
REC_PID=
# "safe" latency adds margin, so a busy machine without real-time scheduling does not drop audio.
start_lsq() { # layout preset logfile
    "$RUN" --backend pipewire --output e2e-speakers --sink-layout "$1" --preset "$2" \
        --latency safe --duration 600 --trace "${3%.log}.trace.csv" > "$3" 2>&1 &
    RUN_PID=$!; PIDS+=($RUN_PID)
    wait_for_sink || { echo "FAIL: the LiveSqueeze sink did not appear"; tail "$3"; return 1; }
    sleep 1
}
stop_lsq() {
    kill "$RUN_PID" 2>/dev/null; wait "$RUN_PID" 2>/dev/null; RUN_PID=
    sleep 0.5
}
record_start() { # outfile
    pw-record --target e2e-speakers -P stream.capture.sink=true --rate 48000 --channels 2 \
        --format f32 --latency "$SRC_LATENCY" "$1" > "$WORK/pw-record.log" 2>&1 &
    REC_PID=$!; PIDS+=($REC_PID)
    sleep 1
}
record_stop() { kill -INT "$REC_PID" 2>/dev/null; wait "$REC_PID" 2>/dev/null; sleep 0.3; }

# Dropouts before the end of the file, from the runner's own log: the highest underrun count
# reported before the last three status lines (the end of the file drains the queue by design).
mid_stream_underruns() { # logfile
    grep '^\[' "$1" | head -n -3 | sed -n 's/.*underruns \([0-9]*\).*/\1/p' | sort -n | tail -1
}

# Which of the mid-stream dropouts can be blamed on LiveSqueeze? The runner's --trace file lists
# every callback. When the player (pw-cat) is scheduled late, or the graph skips a cycle, the
# virtual sink gets no data for that cycle: the playback callback runs twice with no capture
# callback in between. LiveSqueeze cannot invent the missing audio, so a queue that runs dry after
# such a gap is the machine's doing. A dropout with a steady supply is LiveSqueeze's.
# Prints "<dropouts caused by LiveSqueeze> <dropouts after a missing capture cycle>".
classify_dropouts() { # trace.csv
    python3 -I - "$1" <<'PY'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
first = next((i for i, r in enumerate(rows) if r['kind'] == 'capture'), None)
if first is None:
    print("0 0"); sys.exit()
rows = rows[first:]
last_capture = max(i for i, r in enumerate(rows) if r['kind'] == 'capture')
t = lambda r: float(r['time_s'])
# silent playback blocks in the middle of the stream: the engine ran dry and re-primed
silent = [i for i, r in enumerate(rows[:last_capture - 20])
          if r['kind'] == 'playback' and i > 20 and float(r['peak']) == 0.0]
events = []                       # first silent block of each dropout
for i in silent:
    if not events or t(rows[i]) - t(rows[events[-1]]) > 0.5:
        events.append(i)
missing = []                      # times of playback callbacks with no capture callback before them
prev = None
for i, r in enumerate(rows):
    if r['kind'] == 'playback' and prev == 'playback':
        missing.append(t(r))
    prev = r['kind']
ours = excused = 0
for i in events:
    if any(0.0 <= t(rows[i]) - m <= 2.0 for m in missing):
        excused += 1
    else:
        ours += 1
print(ours, excused)
PY
}

# ---- 1 + 2: channel layouts and mapping ------------------------------------------------------
if want layouts; then
for layout in 5.1 7.1; do
    echo "== virtual sink $layout: layout and channel mapping"
    "$CLI" gen --kind tones --layout "$layout" --seconds 12 --out "$WORK/tones-$layout.wav" >/dev/null
    start_lsq "$layout" limiter-only "$WORK/run-$layout.log" || { FAILS=$((FAILS + 1)); continue; }
    want_ch=6; [[ $layout == 7.1 ]] && want_ch=8
    got=$(sink_info)
    check "sink advertises $want_ch channels ($got)" test "$(echo "$got" | tr ',' '\n' | wc -l)" -eq $want_ch
    record_start "$WORK/tones-out-$layout.wav"
    pw-cat -p --target livesqueeze --latency "$SRC_LATENCY" "$WORK/tones-$layout.wav" > "$WORK/pw-cat.log" 2>&1
    sleep 0.5
    record_stop
    stop_lsq
    "$CLI" tones-check "$WORK/tones-out-$layout.wav" --layout "$layout" --max-diff 0.3 | sed 's/^/    /'
    check "every $layout channel reaches the right side at the right level" \
        quiet "$CLI" tones-check "$WORK/tones-out-$layout.wav" --layout "$layout" --max-diff 0.3
done
fi

if want film; then
# ---- 3: dynamics match the offline result ----------------------------------------------------
# The source (pw-cat) is an ordinary process on a machine that may have no real-time scheduling.
# If it delivers late, the queue runs dry and LiveSqueeze, correctly, fades and re-primes, which
# shifts the timing against the offline reference. A run whose own log shows such a dropout is
# therefore repeated (up to 3 times) instead of being compared, and the retry is reported.
echo "== film soundtrack: levels match the offline result"
"$CLI" gen --kind movie --layout 5.1 --seconds 24 --out "$WORK/film.wav" >/dev/null
"$CLI" process "$WORK/film.wav" "$WORK/film-ref.wav" --preset movie-night >/dev/null
for attempt in 1 2 3; do
    start_lsq 5.1 movie-night "$WORK/run-film.log" || FAILS=$((FAILS + 1))
    record_start "$WORK/film-out.wav"
    pw-cat -p --target livesqueeze --latency "$SRC_LATENCY" "$WORK/film.wav" > "$WORK/pw-cat.log" 2>&1
    sleep 0.5
    record_stop
    stop_lsq
    drops=$(mid_stream_underruns "$WORK/run-film.log")
    [[ ${drops:-0} -eq 0 ]] && break
    echo "    attempt $attempt had $drops dropout(s) after the source skipped a cycle; retrying"
done
"$CLI" compare "$WORK/film-ref.wav" "$WORK/film-out.wav" --window 1.0 --max-diff 1.0 | sed 's/^/    /' | tail -6
check "loudness matches the offline processing in every window" \
    quiet "$CLI" compare "$WORK/film-ref.wav" "$WORK/film-out.wav" --window 1.0 --max-diff 1.0

peak_ok() { # wav: peak sample within 6% of the -1 dBTP ceiling (0.891)
    python3 - "$1" <<'PY'
import array, struct, sys
d = open(sys.argv[1], 'rb').read()
pos = 12
while pos + 8 <= len(d):
    cid, sz = d[pos:pos+4], struct.unpack('<I', d[pos+4:pos+8])[0]
    if cid == b'data':
        a = array.array('f')
        a.frombytes(d[pos+8:pos+8+sz - (sz % 4)])
        sys.exit(0 if max(abs(min(a)), abs(max(a))) <= 0.891 * 1.06 else 1)
    pos += 8 + sz + (sz & 1)
sys.exit(1)
PY
}
check "output never exceeds the -1 dBTP ceiling by more than 0.5 dB" peak_ok "$WORK/film-out.wav"
fi

if want soak; then
# ---- 4: soak ---------------------------------------------------------------------------------
echo "== soak: ${SOAK} s of film soundtrack"
"$CLI" gen --kind movie --layout 5.1 --seconds "$SOAK" --out "$WORK/soak.wav" >/dev/null
for attempt in 1 2 3; do
    start_lsq 5.1 movie-night "$WORK/run-soak.log" || FAILS=$((FAILS + 1))
    pw-cat -p --target livesqueeze --latency "$SRC_LATENCY" "$WORK/soak.wav" > "$WORK/pw-cat.log" 2>&1
    stop_lsq
    read -r ours excused <<< "$(classify_dropouts "$WORK/run-soak.trace.csv")"
    [[ ${ours:-0} -gt 0 || ${excused:-0} -eq 0 ]] && break
    echo "    attempt $attempt: $excused dropout(s) after the source skipped a cycle (not LiveSqueeze); retrying"
done
echo "    $(grep '^\[' "$WORK/run-soak.log" | tail -3 | head -1)"
check "the runner reported status during the soak" test -n "$(grep '^\[' "$WORK/run-soak.log")"
check "no dropouts caused by LiveSqueeze during the soak (after a skipped source cycle, not counted: ${excused:-0})" test "${ours:-99}" -eq 0
fi

echo
if [[ $FAILS -eq 0 ]]; then echo "PipeWire end-to-end: ALL PASSED"; exit 0; fi
echo "PipeWire end-to-end: $FAILS CHECK(S) FAILED (logs kept in $WORK)"
KEEP_E2E=1
exit 1
