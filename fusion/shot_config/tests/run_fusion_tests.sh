#!/bin/sh
set -eu

FUSION_APP="/Applications/Blackmagic Fusion 21/Fusion.app"
FUSCRIPT="$FUSION_APP/Contents/Libraries/fuscript"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CLOSE_TEMP="$SCRIPT_DIR/../../test_support/close_temporary_comp.sh"

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

output=$("$FUSCRIPT" -l lua "$SCRIPT_DIR/fusion_host_tests.lua" 2>&1)
printf '%s\n' "$output"
if printf '%s\n' "$output" | grep -F 'FUSION_TEMP_COMP_CREATED' >/dev/null; then
    "$CLOSE_TEMP"
fi
if printf '%s\n' "$output" | grep -E '\.lua:[0-9]+:|stack traceback|attempt to (index|call)' >/dev/null; then
    echo "FusionScript reported a Lua error" >&2
    exit 1
fi
if ! printf '%s\n' "$output" | grep -F 'SHOTCONFIG_FUSION_TESTS_OK' >/dev/null; then
    echo "ShotConfig host tests did not report success" >&2
    exit 1
fi
echo "ShotConfig Fusion 21 acceptance tests passed"
