#!/usr/bin/env bash
# Independent check of the DSP with ffmpeg's EBU R128 meter.
#
# Generates a synthetic 5.1 film soundtrack, downmixes it with ffmpeg (same coefficients as the
# LiveSqueeze default) as the "before" reference, runs lsq-cli on it for the "after", then compares
# loudness range (LRA) and true peak as measured by ffmpeg. Exits non-zero if the processed file
# does not have a lower LRA or if its true peak exceeds -0.9 dBTP.
#
# usage: scripts/xcheck_ffmpeg.sh [path/to/lsq-cli] [extra lsq-cli process options...]
set -euo pipefail

CLI=${1:-build/gcc/cli/lsq-cli}
shift || true
command -v ffmpeg >/dev/null || { echo "ffmpeg not found" >&2; exit 2; }
[[ -x $CLI ]] || { echo "lsq-cli not found at $CLI (build it first)" >&2; exit 2; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

"$CLI" gen --kind movie --layout 5.1 --seconds 60 --out "$TMP/in51.wav" >/dev/null

# Reference: plain downmix, no dynamics. Center 0.7071 * 10^(3/20) = 0.99882 (the default center boost).
ffmpeg -nostdin -v error -y -i "$TMP/in51.wav" \
    -af "pan=stereo|FL=FL+0.99882*FC+0.70711*BL|FR=FR+0.99882*FC+0.70711*BR" \
    -c:a pcm_f32le "$TMP/before.wav"

"$CLI" process "$TMP/in51.wav" "$TMP/after.wav" "$@" >/dev/null

measure() { # file -> "LRA TP"
    ffmpeg -nostdin -hide_banner -i "$1" -af ebur128=peak=true -f null - 2>&1 | awk '
        /LRA:/  { lra = $2 }
        /Peak:/ { tp = $2 }
        END { printf "%s %s\n", lra, tp }'
}

read -r lra_before tp_before < <(measure "$TMP/before.wav")
read -r lra_after tp_after < <(measure "$TMP/after.wav")

printf '%-22s LRA %6s LU   true peak %7s dBFS\n' "downmix only:" "$lra_before" "$tp_before"
printf '%-22s LRA %6s LU   true peak %7s dBFS\n' "after LiveSqueeze:" "$lra_after" "$tp_after"

fail=0
awk -v a="$lra_after" -v b="$lra_before" 'BEGIN { exit !(a < b) }' || { echo "FAIL: LRA did not shrink"; fail=1; }
awk -v tp="$tp_after" 'BEGIN { exit !(tp <= -0.9) }' || { echo "FAIL: true peak above -0.9 dBTP"; fail=1; }
[[ $fail -eq 0 ]] && echo "OK: loudness range reduced, true peak within the ceiling"
exit $fail
