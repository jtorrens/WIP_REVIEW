#!/bin/sh
set -eu

FUSION_APP="/Applications/Blackmagic Fusion 21/Fusion.app"
FUSCRIPT="$FUSION_APP/Contents/Libraries/fuscript"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ ! -x "$FUSCRIPT" ]; then
    echo "FusionScript not found: $FUSCRIPT" >&2
    exit 1
fi

if ! pgrep -x Fusion >/dev/null 2>&1; then
    open -a "$FUSION_APP"
    attempt=0
    while [ "$attempt" -lt 60 ]; do
        if "$FUSCRIPT" -l lua -e 'print("OUTPUTPACKAGER_FUSION_READY")' 2>/dev/null | grep -F 'OUTPUTPACKAGER_FUSION_READY' >/dev/null; then
            break
        fi
        attempt=$((attempt + 1))
        sleep 1
    done
fi

output=$("$FUSCRIPT" -l lua "$SCRIPT_DIR/probe_output_packager.lua" 2>&1)
printf '%s\n' "$output"

if printf '%s\n' "$output" | grep -E '\.lua:[0-9]+:|stack traceback|attempt to (index|call)' >/dev/null; then
    exit 1
fi

printf '%s\n' "$output" | grep -F 'OUTPUTPACKAGER_PROBE_READY' >/dev/null
printf '%s\n' "$output" | grep -F 'requested=Saver regid=Saver' >/dev/null
printf '%s\n' "$output" | grep -F 'requested=BetterResize regid=BetterResize' >/dev/null
printf '%s\n' "$output" | grep -F 'requested=Switch regid=Switch' >/dev/null
printf '%s\n' "$output" | grep -F 'requested=ofx.com.jtorrens.WIPReviewProbe regid=ofx.com.jtorrens.WIPReviewProbe' >/dev/null

if printf '%s\n' "$output" | grep -F 'OUTPUTPACKAGER_PROBE_REQUIRED_MISSING' >/dev/null; then
    exit 1
fi

printf '%s\n' 'OUTPUTPACKAGER_HOST_PROBE_OK'
