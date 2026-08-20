#!/bin/sh
set -eu
FUSCRIPT="/Applications/Blackmagic Fusion 21/Fusion.app/Contents/Libraries/fuscript"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
"$FUSCRIPT" -l lua "$SCRIPT_DIR/saver_versions_host_test.lua"
