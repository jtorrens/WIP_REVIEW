# Windows read-only DirectWrite analysis for macOS

## Scope and source

This is a read-only diagnosis of Windows behaviour reported in Resolve after
installing the current Windows validation branch at `553a612`. No source or
test implementation was changed for this report.

The Windows OFX binary built from that commit and the system-installed binary
are identical:

```
7FBB8B1A4B4B902BE15710463802F99E39570495B817664CFC254489FA27E072
```

Resolve was inspected while running and had loaded exactly:

```
C:\Program Files\Common Files\OFX\Plugins\WIPReviewProbe.ofx.bundle\Contents\Win64\WIPReviewProbe.ofx
```

No second `WIPReviewProbe.ofx.bundle` was found in the standard system,
ProgramData, user OFX, or Resolve locations. The repeated Windows visual
failure is therefore not attributable to the wrong checkout, bundle, or OFX
search path.

## Shared placement is not the suspected fault

Both platforms use `probe_core.cpp` for `computeTextOrigin` and
`compositeTextMask`. That shared code computes:

- left anchors from the padded left frame edge;
- centre anchors from `(frame width - mask width) / 2`;
- right anchors from the padded right edge minus `mask.width`;
- vertical origins and normalized offsets in the same generic function.

It has no platform branch, text-cell width, fit-to-zone scaling, shortening,
or ellipsis path. The text API contract in `text_rasterizer.hpp` likewise
requires a complete string at its requested size. Do not alter this shared
placement logic for Windows.

## DirectWrite evidence and likely fault boundary

The last full-resolution Resolve records available before the current rebuild
showed the expected complete BR string and generic placement:

```
render_window=[0,0,3840,2160]
zone="BR"
resolved_text="The-Fields-Original.braw"
rendered_text="The-Fields-Original.braw"
requested_pixel_size=60.480000
effective_pixel_size=60.480000
resolved_font="Segoe UI"
mask=[652,62]
origin=[3130,43]
```

With the default right padding, `3130 + 652 = 3782`, the padded right edge.
The user nevertheless repeatedly reports that the visible suffix is missing.
The current log has not appended a fresh matching record after the final
install, so the exact post-install raster is not yet captured; the loaded
module and binary identity above are confirmed.

The platform implementations differ only in mask creation:

- macOS derives the allocation from `CTLineGetBoundsWithOptions` using glyph
  path bounds, then places the line into that ink-aware bitmap;
- Windows allocates a DirectWrite GDI bitmap from `DWRITE_TEXT_METRICS.width`
  or `widthIncludingTrailingWhitespace` and `metrics.height`, with a fixed
  four-pixel inset, then crops non-zero BGRA coverage.

Windows does not query `DWRITE_OVERHANG_METRICS`, and it does not verify that
ink reached the target edges before accepting the bitmap. Thus DirectWrite can
produce an incomplete mask even though the complete text and the mask advance
width are correctly logged. This is the only implementation boundary
consistent with the evidence and the shared contract.

## Test gap and requested next action

Current rasterizer tests prove UTF-8 rendering, font fallback, orientation,
and generic complete-string plumbing, but do not assert full right-edge ink
for `The-Fields-Original.braw` at `60.48` pixels on Windows. The generic
anchor tests use synthetic masks, which is appropriate and already confirms
the shared layout contract.

Please review and, if confirmed, make a Windows-only DirectWrite fix that:

1. derives bitmap allocation from ink/overhang-aware bounds;
2. retries with a larger bitmap if non-zero coverage reaches an allocation
   edge;
3. adds a Windows regression that proves the complete `.braw` suffix has
   raster ink; and
4. leaves `probe_core.cpp`, anchor geometry, text sizing, and overflow policy
   unchanged.

After that change, Windows should rebuild from a clean directory, pass CTest,
and validate a newly created Resolve instance with a full-resolution output
and matching log records.
