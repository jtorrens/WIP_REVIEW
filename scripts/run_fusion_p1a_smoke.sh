#!/bin/sh
set -eu

FUSION_APP="/Applications/Blackmagic Fusion 21/Fusion.app"
FUSCRIPT="$FUSION_APP/Contents/Libraries/fuscript"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROBE_LOG="$HOME/Library/Logs/WIPReviewProbe/WIPReviewProbe.log"

if [ ! -x "$FUSCRIPT" ]; then
    echo "FusionScript not found: $FUSCRIPT" >&2
    exit 1
fi

if ! pgrep -f "$FUSION_APP/Contents/MacOS/Fusion" >/dev/null; then
    open -a "$FUSION_APP"
    attempts=0
    while ! pgrep -f "$FUSION_APP/Contents/MacOS/Fusion" >/dev/null; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 60 ]; then
            echo "Fusion did not start within 60 seconds" >&2
            exit 1
        fi
        sleep 1
    done
fi

before=0
if [ -f "$PROBE_LOG" ]; then
    before=$(wc -c < "$PROBE_LOG" | tr -d ' ')
fi

"$FUSCRIPT" -l lua "$SCRIPT_DIR/fusion_p1a_smoke.lua"

if [ ! -f "$PROBE_LOG" ]; then
    echo "Probe log was not created: $PROBE_LOG" >&2
    exit 1
fi

after=$(wc -c < "$PROBE_LOG" | tr -d ' ')
if [ "$after" -le "$before" ]; then
    echo "Fusion rendered no new probe log records" >&2
    exit 1
fi

new_log=$(mktemp -t wipreview-fusion-smoke.XXXXXX)
trap 'rm -f "$new_log"' EXIT
tail -c "+$((before + 1))" "$PROBE_LOG" > "$new_log"

require_record() {
    pattern=$1
    description=$2
    if ! grep -E "$pattern" "$new_log" >/dev/null; then
        echo "Missing $description in automated Fusion log" >&2
        echo "Expected pattern: $pattern" >&2
        exit 1
    fi
}

require_record 'INSTANCE_CREATE .*context="OfxImageEffectContextFilter"' 'Filter context'
require_record 'TEMPORAL_STRING_PROBE .*scenario="AUTOMATED_FUSION_P1A_SMOKE"' 'scenario marker'
require_record 'IMAGE .*clip=Source bounds=\[0,0,4608,3164\]' '4608x3164 Source image'
require_record 'IMAGE .*clip=Output bounds=\[0,0,1920,1080\]' '1920x1080 Output image'
require_record 'RENDER .*render_window=\[0,0,1920,1080\].*render_scale=\[1,1\]' 'full-scale render window'
require_record 'STATIC_FORMATTER .*placement=1 filter=2 .*source_PAR=1\.000000 output_PAR=1\.000000' 'P1a Fit/Lanczos3 formatter pass'

echo "Automated Fusion P1a smoke test passed"
