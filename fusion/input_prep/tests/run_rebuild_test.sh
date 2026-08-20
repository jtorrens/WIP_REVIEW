#!/bin/sh
set -eu

FUSION_APP="/Applications/Blackmagic Fusion 21/Fusion.app"
FUSCRIPT="$FUSION_APP/Contents/Libraries/fuscript"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
EXAMPLE="/private/tmp/InputPrep_Rebuild_Test.comp"

output=$("$FUSCRIPT" -l lua "$SCRIPT_DIR/rebuild_host_test.lua" 2>&1)
printf '%s\n' "$output"
if printf '%s\n' "$output" | grep -E '\.lua:[0-9]+:|stack traceback|attempt to (index|call)' >/dev/null; then
    exit 1
fi
printf '%s\n' "$output" | grep -F 'INPUTPREP_REBUILD_HOST_TEST_OK' >/dev/null
if grep -E 'Expression = ".*(ShotConfig|InputPrepConfig)' "$EXAMPLE" >/dev/null; then
    echo "cross-component expression found in rebuilt comp" >&2
    exit 1
fi
