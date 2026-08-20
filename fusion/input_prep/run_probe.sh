#!/bin/sh
set -eu

FUSION_APP="/Applications/Blackmagic Fusion 21/Fusion.app"
FUSCRIPT="$FUSION_APP/Contents/Libraries/fuscript"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CLOSE_TEMP="$SCRIPT_DIR/../test_support/close_temporary_comp.sh"

output=$("$FUSCRIPT" -l lua "$SCRIPT_DIR/probe_input_prep.lua" 2>&1)
printf '%s\n' "$output"
if printf '%s\n' "$output" | grep -F 'FUSION_TEMP_COMP_CREATED' >/dev/null; then
    "$CLOSE_TEMP"
fi

if printf '%s\n' "$output" | grep -E '\.lua:[0-9]+:|stack traceback|attempt to (index|call)' >/dev/null; then
    exit 1
fi
printf '%s\n' "$output" | grep -F 'INPUTPREP_PROBE_READY' >/dev/null
