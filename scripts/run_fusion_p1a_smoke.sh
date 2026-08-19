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

script_output=$("$FUSCRIPT" -l lua "$SCRIPT_DIR/fusion_p1a_smoke.lua" 2>&1)
printf '%s\n' "$script_output"
if ! printf '%s\n' "$script_output" | grep -F 'WIPREVIEW_AUTOMATION_OK' >/dev/null; then
    echo "FusionScript did not report a successful automated render" >&2
    exit 1
fi

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
require_record 'INSTANCE_CREATE .*context="OfxImageEffectContextGeneral"' 'General context'
require_record 'TEMPORAL_STRING_PROBE .*scenario="AUTOMATED_P1A_FILTER_FIT"' 'Filter scenario marker'
require_record 'TEMPORAL_STRING_PROBE .*scenario="AUTOMATED_P1A_GENERAL_FIT"' 'General scenario marker'
require_record 'TEMPORAL_STRING_PROBE .*scenario="AUTOMATED_P1A_HOST_RASTER"' 'Host Raster scenario marker'
require_record 'TEMPORAL_STRING_PROBE .*scenario="AUTOMATED_P1B_B01_2_00"' 'B01 scenario marker'
require_record 'TEMPORAL_STRING_PROBE .*scenario="AUTOMATED_P1B_B02_HALF"' 'B02 scenario marker'
require_record 'TEMPORAL_STRING_PROBE .*scenario="AUTOMATED_P1B_B03_OFF"' 'B03 scenario marker'
require_record 'TEMPORAL_STRING_PROBE .*scenario="AUTOMATED_P1B_B04_PILLAR"' 'B04 scenario marker'
require_record 'IMAGE .*clip=Source bounds=\[0,0,4608,3164\]' '4608x3164 Source image'
require_record 'IMAGE .*clip=Output bounds=\[0,0,1920,1080\]' '1920x1080 Output image'
require_record 'IMAGE .*clip=Output bounds=\[0,0,4608,3164\]' 'Host Raster Output image'
require_record 'RENDER .*render_window=\[0,0,1920,1080\].*render_scale=\[1,1\]' 'full-scale render window'
for placement in 0 1 2 3 4; do
    require_record "STATIC_FORMATTER .*placement=$placement filter=2 .*source_PAR=1\\.000000 output_PAR=1\\.000000" "P1a placement $placement formatter pass"
done
require_record 'RENDER_WARNING .*identity_raster_mismatch=true implicit_resize=false' 'Identity mismatch warning'
require_record 'EDITORIAL_BLANKING .*enabled=true aspect=2\.000000 output_PAR=1\.000000 aperture=\[0\.000000,60\.000000,1920\.000000,1020\.000000\].*opacity=1\.000000' 'B01 opaque 2.00 letterbox'
require_record 'EDITORIAL_BLANKING .*enabled=true aspect=2\.000000 output_PAR=1\.000000 aperture=\[0\.000000,60\.000000,1920\.000000,1020\.000000\].*opacity=0\.500000' 'B02 half-opacity letterbox'
require_record 'EDITORIAL_BLANKING .*enabled=false' 'B03 blanking disabled'
require_record 'EDITORIAL_BLANKING .*enabled=true aspect=1\.330000 output_PAR=1\.000000 aperture=\[241\.800000,0\.000000,1678\.200000,1080\.000000\].*opacity=1\.000000' 'B04 narrow-aspect pillarbox'

echo "Automated Fusion P1a/P1b smoke test passed"
