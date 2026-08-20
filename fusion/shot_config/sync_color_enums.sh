#!/bin/sh
set -eu

FUSION_APP="/Applications/Blackmagic Fusion 21/Fusion.app"
FUSCRIPT="$FUSION_APP/Contents/Libraries/fuscript"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

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

output=$("$FUSCRIPT" -l lua "$SCRIPT_DIR/sync_color_enums.lua" 2>&1)
printf '%s\n' "$output"
if printf '%s\n' "$output" | grep -E '\.lua:[0-9]+:|stack traceback|attempt to (index|call)' >/dev/null; then
    echo "Color enum synchronization failed" >&2
    exit 1
fi
if ! printf '%s\n' "$output" | grep -F 'SHOTCONFIG_COLOR_ENUM_SYNC_OK' >/dev/null; then
    echo "Color enum synchronization did not report success" >&2
    exit 1
fi
