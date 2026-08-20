#!/bin/sh
set -eu

FUSION_APP="/Applications/Blackmagic Fusion 21/Fusion.app"
FUSCRIPT="$FUSION_APP/Contents/Libraries/fuscript"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CHART_PATH="/private/tmp/wipreview-visual-chart.png"

if [ ! -x "$FUSCRIPT" ]; then
    echo "FusionScript not found: $FUSCRIPT" >&2
    exit 1
fi
if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "ffmpeg is required to generate the visual test chart" >&2
    exit 1
fi

ffmpeg -hide_banner -loglevel error \
    -f lavfi -i 'testsrc2=size=4608x3164:rate=1' \
    -vf 'drawgrid=width=512:height=512:thickness=8:color=white@0.7,drawbox=x=0:y=0:w=500:h=500:color=red:t=fill,drawbox=x=4108:y=0:w=500:h=500:color=green:t=fill,drawbox=x=0:y=2664:w=500:h=500:color=blue:t=fill,drawbox=x=4108:y=2664:w=500:h=500:color=yellow:t=fill,drawbox=x=2204:y=0:w=200:h=3164:color=white@0.8:t=fill,drawbox=x=0:y=1482:w=4608:h=200:color=white@0.8:t=fill' \
    -frames:v 1 -y "$CHART_PATH"

if ! pgrep -f "$FUSION_APP/Contents/MacOS/Fusion" >/dev/null; then
    open -a "$FUSION_APP"
fi

script_output=$(WIPREVIEW_VISUAL_CHART="$CHART_PATH" \
    "$FUSCRIPT" -l lua "$SCRIPT_DIR/fusion_visual.lua" 2>&1)
printf '%s\n' "$script_output"
if ! printf '%s\n' "$script_output" | grep -F 'WIPREVIEW_VISUAL_READY' >/dev/null; then
    echo "Fusion visual-validation composition was not created" >&2
    exit 1
fi

echo "Visual composition ready; select a cumulative P1, P2A, P2B, P2C, P3 or P4 validation node and press 1 or 2."
