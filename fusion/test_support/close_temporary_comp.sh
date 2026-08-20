#!/bin/sh
set -eu

# The runner calls this only after creating a temporary active composition.
# Fusion's remote comp:Close() blocks fuscript, so close through the host UI.
FUSION_APP="/Applications/Blackmagic Fusion 21/Fusion.app"
FUSCRIPT="$FUSION_APP/Contents/Libraries/fuscript"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

"$FUSCRIPT" -l lua "$SCRIPT_DIR/save_temporary_comp_for_close.lua" >/dev/null

osascript \
    -e 'tell application "System Events" to tell process "Fusion" to set frontmost to true' \
    -e 'delay 0.5' \
    -e 'tell application "System Events" to tell process "Fusion" to repeat with fusion_window in every window' \
    -e 'if exists button "OK" of fusion_window then' \
    -e 'click button "OK" of fusion_window' \
    -e 'exit repeat' \
    -e 'end if' \
    -e 'end repeat' \
    -e 'delay 0.5' \
    -e 'tell application "System Events" to tell process "Fusion" to click menu item "Close" of menu "File" of menu bar 1' \
    -e 'delay 0.75' \
    >/dev/null
