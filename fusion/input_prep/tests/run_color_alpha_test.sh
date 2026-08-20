#!/bin/sh
set -eu

FUSION_APP="/Applications/Blackmagic Fusion 21/Fusion.app"
FUSCRIPT="$FUSION_APP/Contents/Libraries/fuscript"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CLOSE_TEMP="$SCRIPT_DIR/../../test_support/close_temporary_comp.sh"

rm -f \
    /private/tmp/inputprep_opaque_actual0000.exr \
    /private/tmp/inputprep_opaque_reference0000.exr \
    /private/tmp/inputprep_embedded_actual0000.exr \
    /private/tmp/inputprep_embedded_reference0000.exr

output=$("$FUSCRIPT" -l lua "$SCRIPT_DIR/color_alpha_host_test.lua" 2>&1)
printf '%s\n' "$output"
if printf '%s\n' "$output" | grep -F 'FUSION_TEMP_COMP_CREATED' >/dev/null; then
    "$CLOSE_TEMP"
fi
if printf '%s\n' "$output" | grep -E '\.lua:[0-9]+:|stack traceback|attempt to (index|call)' >/dev/null; then
    exit 1
fi
printf '%s\n' "$output" | grep -F 'INPUTPREP_COLOR_ALPHA_RENDER_OK' >/dev/null
python3 "$SCRIPT_DIR/verify_color_alpha.py"
