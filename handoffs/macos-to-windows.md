# macOS to Windows text-clipping follow-up

## Geometry confirmation

The macOS Resolve log confirms the same UHD horizontal cells reported by
Windows:

| Zone | Cell bounds |
| --- | --- |
| Left | `[58, 1261]` |
| Centre | `[1299, 2541]` |
| Right | `[2579, 3782]` |

macOS intentionally uses the shared `paddingLeft=0.015`,
`paddingRight=0.015`, and `normalizedZoneGap=0.01` calculation. It does not use
three exact 1280-pixel columns. Do not introduce a Windows-specific cell or
margin rule.

## Windows clipping report

The matching cells do not resolve the reported Windows defect. In Resolve on
Windows, a fresh instance using default typography and overflow settings shows
apparently incomplete calculated fields after the fields are selected. The
supplied screenshot shows the top-left frame value and the bottom-right source
filename case.

The current default overflow mode is `ShrinkToFit`, with
`minimumFontScale=0.60`; therefore an ordinary short frame value should not be
clipped. The platform rasterizers also resolve `System Default` differently:
CoreText uses the macOS system font, while DirectWrite currently selects Segoe
UI. Geometry equality alone cannot establish text-layout equality.

## Evidence requested from Windows

Reproduce the screenshot using a newly created OFX instance at 3840x2160 with
unchanged defaults. Record the expected complete frame value and complete
source filename, then return the matching log records from the same render:

- `CALCULATED_FIELDS`
- `TEXT_OVERFLOW`
- `TEXT_ZONE` for `TL`
- `TEXT_ZONE` for `BR`
- `CALCULATED_FIELD_ZONE` for `TL` and `BR`

The records must retain at least these fields: `resolved_text`,
`rendered_text`, `requested_font`, `resolved_font`, `effective_pixel_size`,
`effective_scale`, `overflowed`, `clipped`, `ellipsized`, `cell`, `mask`, and
`origin`.

Also save a full-resolution output still rather than only a scaled Resolve
viewer screenshot. This distinguishes a render defect from viewer scaling.

## Decision procedure

1. If `resolved_text` is already incomplete, inspect the Resolve metadata
   values and calculated-field resolver. Do not change cell geometry or the
   rasterizer.
2. If `resolved_text` is complete but `rendered_text` is incomplete, inspect
   the shared overflow implementation.
3. If both strings are complete and `clipped=false`, inspect the DirectWrite
   ink bounds and bitmap allocation/cropping. In particular, compare
   `IDWriteTextLayout::GetMetrics` with the glyph overhang bounds; the Windows
   raster surface must include negative left bearings and right-side ink
   overhang before cropping.
4. If `clipped=true`, report the mask width, cell width, and effective scale.
   Confirm why `ShrinkToFit` reached its minimum before changing the current
   cross-platform contract.

Any fix should remain host-neutral above the platform rasterizer boundary. A
Windows-only correction is appropriate only inside the DirectWrite
rasterizer when it makes that implementation conform to the shared text-layout
contract. Do not add a compatibility path or legacy fallback.
