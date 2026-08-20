#!/bin/sh
set -eu

FUSION_APP="/Applications/Blackmagic Fusion 21/Fusion.app"
FUSCRIPT="$FUSION_APP/Contents/Libraries/fuscript"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
EXAMPLE="$SCRIPT_DIR/../examples/OutputPackager_Example.comp"

output=$("$FUSCRIPT" -l lua "$SCRIPT_DIR/example_host_test.lua" 2>&1)
printf '%s\n' "$output"
if printf '%s\n' "$output" | grep -E '\.lua:[0-9]+:|stack traceback|attempt to (index|call)' >/dev/null; then
    exit 1
fi
printf '%s\n' "$output" | grep -F 'OUTPUTPACKAGER_EXAMPLE_HOST_TEST_OK' >/dev/null
grep -F 'Group_OutputPackager_ClientReview = GroupOperator {' "$EXAMPLE" >/dev/null
grep -F 'Group_OutputPackager_CleanReview = GroupOperator {' "$EXAMPLE" >/dev/null
grep -F 'Saver_ClientReview = Saver {' "$EXAMPLE" >/dev/null
grep -F 'Saver_CleanReview = Saver {' "$EXAMPLE" >/dev/null
grep -F 'PipeRouter_Input = PipeRouter {' "$EXAMPLE" >/dev/null
grep -F 'BetterResize_ReviewRaster = BetterResize {' "$EXAMPLE" >/dev/null
grep -F 'Background_ReviewCanvas = Background {' "$EXAMPLE" >/dev/null
grep -F 'Merge_ReviewRaster = Merge {' "$EXAMPLE" >/dev/null
grep -F 'Switch_ReviewRaster = Switch {' "$EXAMPLE" >/dev/null
grep -F 'WIPReviewProbe_WIP = ofx.com.jtorrens.WIPReviewProbe {' "$EXAMPLE" >/dev/null
grep -F 'Switch_WIP = Switch {' "$EXAMPLE" >/dev/null
