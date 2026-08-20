#!/bin/sh
set -eu

# The runner calls this only after creating a temporary active composition.
# Fusion's remote comp:Close() blocks fuscript, so close through the host UI.
osascript \
    -e 'tell application "Fusion" to activate' \
    -e 'delay 0.2' \
    -e 'tell application "System Events" to tell process "Fusion" to keystroke "w" using command down' \
    -e 'delay 0.3' \
    -e 'tell application "System Events" to tell process "Fusion" to if exists button "No" of window 1 then click button "No" of window 1' \
    >/dev/null
