#!/bin/sh
set -eu

FUSION_APP="/Applications/Blackmagic Fusion 21/Fusion.app"
FUSCRIPT="$FUSION_APP/Contents/Libraries/fuscript"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
EXAMPLE="$SCRIPT_DIR/../examples/InputPrep_Example.comp"

output=$("$FUSCRIPT" -l lua "$SCRIPT_DIR/example_host_test.lua" 2>&1)
printf '%s\n' "$output"
if printf '%s\n' "$output" | grep -E '\.lua:[0-9]+:|stack traceback|attempt to (index|call)' >/dev/null; then
    exit 1
fi
printf '%s\n' "$output" | grep -F 'INPUTPREP_EXAMPLE_TEST_OK' >/dev/null
grep -F 'Width = Input { Value = 3840, }' "$EXAMPLE" >/dev/null
grep -F 'Height = Input { Value = 2160, }' "$EXAMPLE" >/dev/null
grep -F 'KeepAspect = Input { Value = 1, }' "$EXAMPLE" >/dev/null
grep -F 'KeepCentered = Input { Value = 1, }' "$EXAMPLE" >/dev/null
grep -F 'XSize = Input { Value = 3840, }' "$EXAMPLE" >/dev/null
grep -F 'YSize = Input { Value = 1920, }' "$EXAMPLE" >/dev/null
grep -F 'ToAlpha = Input { Value = 16, }' "$EXAMPLE" >/dev/null
grep -F 'ChangeDepth_Working = ChangeDepth {' "$EXAMPLE" >/dev/null
grep -F 'PipeRouter_Input = PipeRouter {' "$EXAMPLE" >/dev/null
grep -F 'ColorSpaceTransform_Working = ColorSpaceTransform {' "$EXAMPLE" >/dev/null
grep -F 'BetterResize_Working = BetterResize {' "$EXAMPLE" >/dev/null
grep -F 'Crop_Working = Crop {' "$EXAMPLE" >/dev/null
grep -F 'ChannelBoolean_AlphaPolicy = ChannelBoolean {' "$EXAMPLE" >/dev/null
grep -F 'Depth = Input { Value = 3, }' "$EXAMPLE" >/dev/null
grep -F 'SourceOp = "Switch_Depth"' "$EXAMPLE" >/dev/null
grep -F 'SourceOp = "Switch_Color"' "$EXAMPLE" >/dev/null
grep -F 'SourceOp = "Switch_Resize"' "$EXAMPLE" >/dev/null
grep -F 'SourceOp = "Switch_Crop"' "$EXAMPLE" >/dev/null
grep -F 'SourceOp = "Switch_Alpha"' "$EXAMPLE" >/dev/null
