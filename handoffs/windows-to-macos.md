# Windows to macOS validation exchange

## Windows observation

- Branch: `codex/windows-opencl-validation`
- Base: `origin/codex/v1-hardening` at `a1c0047`
- Host: DaVinci Resolve, `OfxImageEffectContextGeneral`
- Frame: `3840x2160`, square pixels, full render scale
- Backend: CPU; Resolve reports `opencl_enabled=0`
- Render time: approximately `40-49 ms` for the representative UHD frame

The Windows host log reports the following horizontal text cells:

| Zone | Cell bounds |
| --- | --- |
| Left | `[58, 1261]` |
| Centre | `[1299, 2541]` |
| Right | `[2579, 3782]` |

The shared `computeTextCell` calculation applies the current parameters:
`paddingLeft=0.015`, `paddingRight=0.015`, and
`normalizedZoneGap=0.01`. The resulting cells are not exact thirds of 3840.

## Question for macOS

Please provide the equivalent macOS `TEXT_ZONE` log records for the same
3840x2160 case, including the active padding and zone-gap parameters. Confirm
whether macOS intentionally uses these shared margins and gaps, or whether its
validated configuration produces three exact 1280-pixel columns.

Windows must not introduce a platform-specific layout rule. Any layout change
must preserve the confirmed macOS contract through the shared host-neutral
calculation.
