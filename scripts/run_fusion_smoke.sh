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
fusion_pid=$(pgrep -f "$FUSION_APP/Contents/MacOS/Fusion" | head -n 1)

before=0
if [ -f "$PROBE_LOG" ]; then
    before=$(wc -c < "$PROBE_LOG" | tr -d ' ')
fi

script_output=$("$FUSCRIPT" -l lua "$SCRIPT_DIR/fusion_smoke.lua" 2>&1)
printf '%s\n' "$script_output"
if printf '%s\n' "$script_output" | grep -E '\.lua:[0-9]+:|stack traceback|attempt to (index|call)' >/dev/null; then
    echo "FusionScript reported a Lua error" >&2
    exit 1
fi
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

require_session_record() {
    pattern=$1
    description=$2
    if ! grep -E "pid=$fusion_pid \\| $pattern" "$PROBE_LOG" >/dev/null; then
        echo "Missing $description for active Fusion PID $fusion_pid" >&2
        echo "Expected pattern: $pattern" >&2
        exit 1
    fi
}

require_session_record 'SUITES \\| .*multithread_v1=true' 'OFX host multithread suite'
require_session_record 'HOST_GPU_CAPABILITIES \\| metal="true"' 'Fusion Metal render capability'
require_session_record 'HOST_RASTER_CAPABILITY \\| requested_review_raster=true' 'Fusion requested-raster capability'
require_record 'INSTANCE_CREATE .*context="OfxImageEffectContextGeneral"' 'General context'
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
require_record 'TEXT_ZONE .*zone="TL" enabled=true prefix="SECUENCIA ÁRTICO — VERSIÓN 03" .*requested_font="System Default" resolved_font="[^"]+" fallback=false normalized_size=0\.028000 requested_pixel_size=30\.240000 effective_pixel_size=30\.240000 .*mask=\[[1-9][0-9]*,[1-9][0-9]*\]' 'UTF-8 text rasterization'
require_record 'TEXT_ZONE .*zone="TL" enabled=true prefix="TOP GROWS DOWN" .*normalized_size=0\.056000 requested_pixel_size=60\.480000 effective_pixel_size=60\.480000 .*mask=\[[1-9][0-9]*,[1-9][0-9]*\]' 'top growth text'
require_record 'TEXT_ZONE .*zone="BL" enabled=true prefix="BOTTOM GROWS UP" .*requested_pixel_size=60\.480000 effective_pixel_size=60\.480000 .*origin=\[29,22\]' 'bottom anchor and padding'
require_record 'TEXT_ZONE .*zone="TR" enabled=true prefix="FONT FALLBACK" .*fallback=true .*mask=\[[1-9][0-9]*,[1-9][0-9]*\]' 'missing-font fallback'
require_record 'TEXT_ZONE .*zone="TC" enabled=true prefix="TEXT OVER BLANKING" .*mask=\[[1-9][0-9]*,[1-9][0-9]*\]' 'text after blanking'
for zone in TL TC TR BL BC BR; do
    require_record "TEXT_ZONE .*zone=\"$zone\" enabled=true prefix=\"$zone[^\"]*\" .*normalized_size=0\.028000 requested_pixel_size=30\.240000 effective_pixel_size=30\.240000 .*mask=\[[1-9][0-9]*,[1-9][0-9]*\]" "P2a zone $zone"
done
require_record 'TEXT_ZONE .*zone="TL" enabled=true prefix="TL LARGE" .*use_size_override=true .*normalized_size=0\.056000 requested_pixel_size=60\.480000 effective_pixel_size=60\.480000' 'P2a size override'
require_record 'TEXT_ZONE .*zone="TC" enabled=true prefix="TC GREEN" .*use_color_override=true .*colour=\[0\.000000,1\.000000,0\.000000,1\.000000\]' 'P2a colour override'
require_record 'TEXT_ZONE .*zone="BR" enabled=true prefix="BR 25%" .*use_opacity_override=true .*opacity=0\.250000' 'P2a opacity override'
require_record 'TEXT_ZONE .*zone="BL" enabled=true prefix="BL OFFSET" .*origin=\[125,65\] offset=\[0\.050000,0\.040000\]' 'P2a normalized offsets'
require_record 'TEXT_ZONE .*zone="TL" enabled=true prefix="TL OVER BLANKING" .*mask=\[[1-9][0-9]*,[1-9][0-9]*\]' 'P2a zone over blanking'
require_record 'TEXT_ZONE .*zone="BR" enabled=true prefix="BR OVER BLANKING" .*mask=\[[1-9][0-9]*,[1-9][0-9]*\]' 'P2a opposite zone over blanking'
require_record 'TEXT_OUTLINE .*enabled=true normalized_width=0\.001000 pixel_radius=1 colour=\[0\.000000,0\.000000,0\.000000,1\.000000\] opacity=1\.000000' 'P2b default outline settings'
require_record 'TEXT_ZONE .*zone="TL" enabled=true prefix="P2B DEFAULT OUTLINE" .*outline=true' 'P2b default glyph outline'
require_record 'TEXT_OUTLINE .*enabled=true normalized_width=0\.006000 pixel_radius=6 colour=\[1\.000000,0\.000000,0\.000000,1\.000000\] opacity=0\.500000' 'P2b wide red half-opacity outline settings'
require_record 'TEXT_ZONE .*zone="TC" enabled=true prefix="P2B RED 50% OUTLINE" .*outline=true' 'P2b wide glyph outline'
require_record 'TEXT_SHADOW .*enabled=true normalized_offset=\[0\.001500,0\.002000\] pixel_offset=\[3,2\] normalized_softness=0\.002000 pixel_softness=2\.160000 colour=\[0\.000000,0\.000000,0\.000000,1\.000000\] opacity=0\.600000' 'P2c default shadow settings'
require_record 'TEXT_ZONE .*zone="TL" enabled=true prefix="P2C DEFAULT SHADOW" .*shadow=true' 'P2c default glyph shadow'
require_record 'TEXT_SHADOW .*enabled=true normalized_offset=\[-0\.010000,-0\.010000\] pixel_offset=\[-19,-11\] normalized_softness=0\.000000 pixel_softness=0\.000000 colour=\[0\.000000,0\.000000,1\.000000,1\.000000\] opacity=1\.000000' 'P2c hard blue shadow settings'
require_record 'TEXT_ZONE .*zone="BR" enabled=true prefix="P2C HARD BLUE SHADOW" .*shadow=true' 'P2c hard glyph shadow'
require_record 'TEXT_ZONE .*zone="TL" enabled=true prefix="FREE ANCHOR KEEPS THIS COMPLETE UTF-8 STRING ÁRTICO OUTSIDE THE FRAME" .*rendered_text="FREE ANCHOR KEEPS THIS COMPLETE UTF-8 STRING ÁRTICO OUTSIDE THE FRAME" .*requested_pixel_size=64\.800000 effective_pixel_size=64\.800000 .*origin=\[29,' 'complete free-anchor text'
require_record 'GET_CLIP_PREFERENCES .*output_frame_varying=false' 'static output frame-varying false'
require_record 'GET_CLIP_PREFERENCES .*output_frame_varying=true' 'calculated output frame-varying true'
require_record 'CALCULATED_FIELD_ZONE .*zone="TL" prefix="REL " resolved="REL 1" effect_frame=0 frame_rel=1 frame=1001 timecode="00:00:00:00" nominal_fps=24 .*used_timecode_fallback=false' 'P3 Frame Relative at frame zero'
require_record 'CALCULATED_FIELD_ZONE .*zone="TL" prefix="REL " resolved="REL 2" effect_frame=1 frame_rel=2 frame=1002 timecode="00:00:00:01" nominal_fps=24 .*used_timecode_fallback=false' 'P3 calculated field increments exactly one frame'
require_record 'TEXT_ZONE .*zone="BC" enabled=true prefix="LITERAL \{frame\}" .*resolved_text="LITERAL \{frame\}"' 'P3 free text remains literal'
require_record 'GPU_RENDER .*backend=metal status=0' 'successful Metal render'
require_record 'MANAGED_COLOR .*mode=1 .*display_encoding="Rec\.709 Gamma 2\.4" graphics_white_mode=0 graphics_white_nits=100\.000000 .*working_space=display-light-linear working_premult=true decode_count=1 .*sampler_weights=precomputed backend=metal .*encode_count=1' 'Rec.709 Metal render'
require_record 'MANAGED_COLOR .*mode=1 .*display_encoding="Rec\.2100 PQ" graphics_white_mode=0 graphics_white_nits=203\.000000 .*working_space=display-light-linear working_premult=true decode_count=1 .*sampler_weights=precomputed backend=metal .*encode_count=1' 'PQ Metal Graphics White'
require_record 'MANAGED_COLOR .*mode=1 .*display_encoding="Rec\.2100 HLG" graphics_white_mode=0 graphics_white_nits=203\.000000 hlg_peak_nits=1000\.000000 .*decode_count=1 .*sampler_weights=precomputed backend=metal .*encode_count=1' 'HLG Metal Graphics White'
require_record 'MANAGED_COLOR .*mode=0 .*host_recognized=false used_manual_interpretation=true display_encoding="Rec\.709 Gamma 2\.4"' 'P4 Auto unknown deterministic interpretation'
require_record 'RENDER_WARNING .*unknown_host_colourspace=.*manual_interpretation="Rec\.709 Gamma 2\.4"' 'P4 Auto unknown warning'
require_record 'GET_CLIP_PREFERENCES .*color_space_mode=1 manual_color_space="Rec\.2100 PQ" requested_source_colourspace="rec2100_pq_display" preferred_colourspace_status=OK' 'P4 manual PQ OFX preference'
# Fusion Studio 21.0.4 does not invoke GetOutputColourspace. P0 already
# records that host limitation; the action remains implemented for hosts that
# negotiate OFX 1.5.1 output colourspace.

echo "Automated Fusion cumulative V1 smoke test passed"
