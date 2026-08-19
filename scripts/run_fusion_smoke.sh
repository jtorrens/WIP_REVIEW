#!/bin/sh
set -eu

FUSION_APP="/Applications/Blackmagic Fusion 21/Fusion.app"
FUSCRIPT="$FUSION_APP/Contents/Libraries/fuscript"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROBE_LOG="$HOME/Library/Logs/WIPReviewProbe/WIPReviewProbe.log"

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

before=0
if [ -f "$PROBE_LOG" ]; then
    before=$(wc -c < "$PROBE_LOG" | tr -d ' ')
fi

script_output=$("$FUSCRIPT" -l lua "$SCRIPT_DIR/fusion_smoke.lua" 2>&1)
printf '%s\n' "$script_output"
if ! printf '%s\n' "$script_output" | grep -F 'WIPREVIEW_AUTOMATION_OK' >/dev/null; then
    echo "FusionScript did not report a successful automated render" >&2
    exit 1
fi

if [ ! -f "$PROBE_LOG" ]; then
    echo "Probe log was not created: $PROBE_LOG" >&2
    exit 1
fi

after=$(wc -c < "$PROBE_LOG" | tr -d ' ')
if [ "$after" -le "$before" ]; then
    echo "Fusion rendered no new probe log records" >&2
    exit 1
fi

new_log=$(mktemp -t wipreview-fusion-smoke.XXXXXX)
trap 'rm -f "$new_log"' EXIT
tail -c "+$((before + 1))" "$PROBE_LOG" > "$new_log"

require_record() {
    pattern=$1
    description=$2
    if ! grep -E "$pattern" "$new_log" >/dev/null; then
        echo "Missing $description in automated Fusion log" >&2
        echo "Expected pattern: $pattern" >&2
        exit 1
    fi
}

require_record 'INSTANCE_CREATE .*context="OfxImageEffectContextFilter"' 'Filter context'
require_record 'INSTANCE_CREATE .*context="OfxImageEffectContextGeneral"' 'General context'
require_record 'TEMPORAL_STRING_PROBE .*scenario="AUTOMATED_GEOMETRY_FILTER_FIT"' 'Filter scenario marker'
require_record 'TEMPORAL_STRING_PROBE .*scenario="AUTOMATED_GEOMETRY_GENERAL_FIT"' 'General scenario marker'
require_record 'TEMPORAL_STRING_PROBE .*scenario="AUTOMATED_GEOMETRY_HOST_RASTER"' 'Host Raster scenario marker'
# Fusion 21 may clear scenarioLabel after preset blanking changes; validate
# those cases from the renderer's explicit geometry record instead. Custom
# currently preserves the string marker and remains an end-to-end check.
require_record 'TEMPORAL_STRING_PROBE .*scenario="AUTOMATED_BLANKING_B04_PILLAR"' 'B04 scenario marker'
require_record 'IMAGE .*clip=Source bounds=\[0,0,4608,3164\]' '4608x3164 Source image'
require_record 'IMAGE .*clip=Output bounds=\[0,0,1920,1080\]' '1920x1080 Output image'
require_record 'IMAGE .*clip=Output bounds=\[0,0,4608,3164\]' 'Host Raster Output image'
require_record 'RENDER .*render_window=\[0,0,1920,1080\].*render_scale=\[1,1\]' 'full-scale render window'
for placement in 0 1 2 3 4; do
    require_record "STATIC_FORMATTER .*placement=$placement filter=2 .*source_PAR=1\\.000000 output_PAR=1\\.000000" "geometry placement $placement formatter pass"
done
require_record 'RENDER_WARNING .*identity_raster_mismatch=true implicit_resize=false' 'Identity mismatch warning'
require_record 'EDITORIAL_BLANKING .*enabled=true aspect=2\.000000 output_PAR=1\.000000 aperture=\[0\.000000,60\.000000,1920\.000000,1020\.000000\].*opacity=1\.000000' 'B01 opaque 2.00 letterbox'
require_record 'EDITORIAL_BLANKING .*enabled=true aspect=2\.000000 output_PAR=1\.000000 aperture=\[0\.000000,60\.000000,1920\.000000,1020\.000000\].*opacity=0\.500000' 'B02 half-opacity letterbox'
require_record 'EDITORIAL_BLANKING .*enabled=false' 'B03 blanking disabled'
require_record 'EDITORIAL_BLANKING .*enabled=true aspect=1\.330000 output_PAR=1\.000000 aperture=\[241\.800000,0\.000000,1678\.200000,1080\.000000\].*opacity=1\.000000' 'B04 narrow-aspect pillarbox'
require_record 'TEXT_ZONE .*zone="TL" enabled=true text="SECUENCIA ÁRTICO — VERSIÓN 03" .*requested_font="System Default" resolved_font="[^"]+" fallback=false normalized_size=0\.028000 requested_pixel_size=30\.240000 effective_pixel_size=30\.240000 .*mask=\[[1-9][0-9]*,[1-9][0-9]*\]' 'UTF-8 text rasterization'
require_record 'TEXT_ZONE .*zone="TL" enabled=true text="TOP GROWS DOWN" .*normalized_size=0\.056000 requested_pixel_size=60\.480000 effective_pixel_size=60\.480000 .*mask=\[[1-9][0-9]*,[1-9][0-9]*\]' 'top growth text'
require_record 'TEXT_ZONE .*zone="BL" enabled=true text="BOTTOM GROWS UP" .*requested_pixel_size=60\.480000 effective_pixel_size=60\.480000 .*origin=\[29,22\]' 'bottom anchor and padding'
require_record 'TEXT_ZONE .*zone="TR" enabled=true text="FONT FALLBACK" .*fallback=true .*mask=\[[1-9][0-9]*,[1-9][0-9]*\]' 'missing-font fallback'
require_record 'TEXT_ZONE .*zone="TC" enabled=true text="TEXT OVER BLANKING" .*mask=\[[1-9][0-9]*,[1-9][0-9]*\]' 'text after blanking'
for zone in TL TC TR BL BC BR; do
    require_record "TEXT_ZONE .*zone=\"$zone\" enabled=true text=\"$zone[^\"]*\" .*normalized_size=0\.028000 requested_pixel_size=30\.240000 effective_pixel_size=30\.240000 .*mask=\[[1-9][0-9]*,[1-9][0-9]*\]" "P2a zone $zone"
done
require_record 'TEXT_ZONE .*zone="TL" enabled=true text="TL LARGE" .*use_size_override=true .*normalized_size=0\.056000 requested_pixel_size=60\.480000 effective_pixel_size=60\.480000' 'P2a size override'
require_record 'TEXT_ZONE .*zone="TC" enabled=true text="TC GREEN" .*use_color_override=true .*colour=\[0\.000000,1\.000000,0\.000000,1\.000000\]' 'P2a colour override'
require_record 'TEXT_ZONE .*zone="BR" enabled=true text="BR 25%" .*use_opacity_override=true .*opacity=0\.250000' 'P2a opacity override'
require_record 'TEXT_ZONE .*zone="BL" enabled=true text="BL OFFSET" .*origin=\[125,65\] offset=\[0\.050000,0\.040000\]' 'P2a normalized offsets'
require_record 'TEXT_ZONE .*zone="TL" enabled=true text="TL OVER BLANKING" .*mask=\[[1-9][0-9]*,[1-9][0-9]*\]' 'P2a zone over blanking'
require_record 'TEXT_ZONE .*zone="BR" enabled=true text="BR OVER BLANKING" .*mask=\[[1-9][0-9]*,[1-9][0-9]*\]' 'P2a opposite zone over blanking'
require_record 'TEXT_OUTLINE .*enabled=true normalized_width=0\.001000 pixel_radius=1 colour=\[0\.000000,0\.000000,0\.000000,1\.000000\] opacity=1\.000000' 'P2b default outline settings'
require_record 'TEXT_ZONE .*zone="TL" enabled=true text="P2B DEFAULT OUTLINE" .*outline=true' 'P2b default glyph outline'
require_record 'TEXT_OUTLINE .*enabled=true normalized_width=0\.006000 pixel_radius=6 colour=\[1\.000000,0\.000000,0\.000000,1\.000000\] opacity=0\.500000' 'P2b wide red half-opacity outline settings'
require_record 'TEXT_ZONE .*zone="TC" enabled=true text="P2B RED 50% OUTLINE" .*outline=true' 'P2b wide glyph outline'
require_record 'TEXT_SHADOW .*enabled=true normalized_offset=\[0\.001500,0\.002000\] pixel_offset=\[3,2\] normalized_softness=0\.002000 pixel_softness=2\.160000 colour=\[0\.000000,0\.000000,0\.000000,1\.000000\] opacity=0\.600000' 'P2c default shadow settings'
require_record 'TEXT_ZONE .*zone="TL" enabled=true text="P2C DEFAULT SHADOW" .*shadow=true' 'P2c default glyph shadow'
require_record 'TEXT_SHADOW .*enabled=true normalized_offset=\[-0\.010000,-0\.010000\] pixel_offset=\[-19,-11\] normalized_softness=0\.000000 pixel_softness=0\.000000 colour=\[0\.000000,0\.000000,1\.000000,1\.000000\] opacity=1\.000000' 'P2c hard blue shadow settings'
require_record 'TEXT_ZONE .*zone="BR" enabled=true text="P2C HARD BLUE SHADOW" .*shadow=true' 'P2c hard glyph shadow'
require_record 'TEXT_OVERFLOW .*mode=0 normalized_zone_gap=0\.010000 minimum_font_scale=0\.600000 minimum_policy=clip' 'P2d Clip policy'
require_record 'TEXT_ZONE .*zone="TL" enabled=true text="CLIP KEEPS THIS COMPLETE UTF-8 STRING ÁRTICO OUTSIDE CELL" .*rendered_text="CLIP KEEPS THIS COMPLETE UTF-8 STRING ÁRTICO OUTSIDE CELL" .*effective_scale=1\.000000 overflowed=true clipped=true ellipsized=false cell=\[29,630\]' 'P2d Clip result'
require_record 'TEXT_OVERFLOW .*mode=1 normalized_zone_gap=0\.010000 minimum_font_scale=0\.600000 minimum_policy=clip' 'P2d Ellipsis policy'
require_record 'TEXT_ZONE .*zone="TC" enabled=true text="ELLIPSIS PRESERVES UTF-8 ÁRTICO AND REPLACES THE TAIL" .*rendered_text="[^"]*…" .*effective_scale=1\.000000 overflowed=true clipped=false ellipsized=true cell=\[650,1270\]' 'P2d UTF-8 Ellipsis result'
require_record 'TEXT_OVERFLOW .*mode=2 normalized_zone_gap=0\.010000 minimum_font_scale=0\.600000 minimum_policy=clip' 'P2d ShrinkToFit policy'
require_record 'TEXT_ZONE .*zone="TR" enabled=true text="SHRINK TO FIT THIS CELL" .*effective_scale=0\.[6-9][0-9]* overflowed=true clipped=false ellipsized=false cell=\[1290,1891\]' 'P2d ShrinkToFit result'
require_record 'TEXT_ZONE .*zone="BL" enabled=true text="MINIMUM SCALE STILL CANNOT FIT THIS DELIBERATELY VERY LONG STRING AND MUST CLIP" .*effective_scale=0\.600000 overflowed=true clipped=true ellipsized=false cell=\[29,630\]' 'P2d minimum-scale Clip result'
require_record 'GET_CLIP_PREFERENCES .*output_frame_varying=false' 'static output frame-varying false'
require_record 'GET_CLIP_PREFERENCES .*output_frame_varying=true' 'token output frame-varying true'
require_record 'TOKEN_ZONE .*zone="TL" source="REL \{frame_rel\} ABS \{frame\} TC \{timecode\} UNKNOWN \{shot\}" resolved="REL 1 ABS 1001 TC 00:00:00:00 UNKNOWN \{shot\}" effect_frame=0 frame_rel=1 frame=1001 timecode="00:00:00:00" nominal_fps=24 .*used_timecode_fallback=false' 'P3 tokens at frame zero'
require_record 'TOKEN_ZONE .*zone="TL" .*resolved="REL 2 ABS 1002 TC 00:00:00:01 UNKNOWN \{shot\}" effect_frame=1 frame_rel=2 frame=1002 timecode="00:00:00:01" nominal_fps=24 .*used_timecode_fallback=false' 'P3 tokens increment exactly one frame'
require_record 'TOKEN_ZONE .*zone="TC" source="DF \{timecode\}" resolved="DF 00:00:59;29" effect_frame=1799 .*drop_compatible=true drop_applied=true .*used_timecode_fallback=false' 'P3 29.97 DF before minute boundary'
require_record 'TOKEN_ZONE .*zone="TC" source="DF \{timecode\}" resolved="DF 00:01:00;02" effect_frame=1800 .*drop_compatible=true drop_applied=true .*used_timecode_fallback=false' 'P3 29.97 DF minute boundary'
require_record 'TOKEN_ZONE .*zone="BR" source="INVALID TC \{timecode\}" resolved="INVALID TC 00:00:00:01" effect_frame=0 frame_rel=1 .*fps_valid=true drop_compatible=false drop_applied=false timecode_start_valid=false used_timecode_fallback=true' 'P3 invalid timecode controlled fallback'
require_record 'RENDER_WARNING .*timecode_resolution_fallback=true' 'P3 invalid timecode warning'

echo "Automated Fusion cumulative P1/P3 smoke test passed"
