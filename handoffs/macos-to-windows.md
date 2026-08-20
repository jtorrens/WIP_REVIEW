# macOS to Windows free-anchor validation handoff

## CI artifact handoff

The repository is public. GitHub Actions now builds and tests the exact
Windows and macOS OFX bundles on every push to this branch and to
`codex/v1-hardening`.

The first complete cross-platform validation is green:

- run: https://github.com/jtorrens/WIP_REVIEW/actions/runs/32393291653;
- Windows artifact: `WIPReviewProbe-windows-x64`;
- macOS artifact: `WIPReviewProbe-macos`.

Use the Windows artifact from a green run for the first Resolve/Fusion host
validation. Its contents are the tested `.ofx.bundle`; do not install it until
all prerequisites for that host-validation cycle are ready. The artifact is
retained for 14 days.

GitHub Actions validates the MSVC build, CTest, and bundle load smoke test. It
cannot validate Resolve or Fusion host behaviour. Those are the remaining
Windows checks and must be recorded in the return handoff.

## Source contract

The current Windows validation branch is
`codex/windows-opencl-validation` at commit `263da4a`. It already contains
the current source contract and the CI portability fixes.

Before changing or validating Windows code, update the existing local branch:

```powershell
git fetch origin --prune
git switch codex/windows-opencl-validation
git pull --ff-only origin codex/windows-opencl-validation
git rev-parse HEAD
```

Do not create a new branch from `codex/v1-hardening`: use the published
Windows validation branch so its results and Windows-only commits stay
isolated.

The current contract has:

- six independent frame anchors: TL, TC, TR, BL, BC, and BR;
- complete text rasterization at the user-selected size;
- outer left/right/top/bottom padding and per-zone offsets;
- user responsibility for overlap and text extending beyond the output frame.

The updated source tree is the only contract. Validate with newly created OFX
instances.

## Required Windows build validation

Use a clean build directory after updating the branch:

1. Configure and build Release x64 with Visual Studio 2022.
2. Run the complete CTest suite.
3. Confirm the OpenCL host layer and embedded kernel compile with the new
   64-byte `OpenCLLayer` layout.
4. Close Resolve and Fusion, then install the newly built bundle as the last
   build-validation step.

Do not copy a macOS bundle or reuse an older Windows build directory.

## Resolve validation

Create a new OFX instance in Resolve rather than loading an instance saved
with the superseded parameter contract. Use a 3840x2160 output, square pixels,
full render scale, default typography, and select calculated fields in at
least TL and BR.

Confirm from the image and matching log records that:

- `resolved_text` and `rendered_text` contain the complete expected values;
- `requested_pixel_size` equals `effective_pixel_size`;
- `TEXT_ZONE` contains the current resolved text, raster, font, mask, origin,
  offset, color, and opacity fields;
- TL origin X is the left padding plus its offset;
- TC origin X is `(frame width - mask width) / 2` plus its offset;
- TR/BR origin X is the right padded edge minus mask width plus its offset;
- enabling multiple zones never changes another zone's origin;
- long strings may overlap or leave the frame without being rewritten.

Save a full-resolution output still and the matching `CALCULATED_FIELDS`,
`CALCULATED_FIELD_ZONE`, and `TEXT_ZONE` records. A scaled Resolve viewer
screenshot alone is insufficient to diagnose glyph rasterization.

Resolve previously reported `opencl_enabled=0`; if that remains true, record
the test as CPU validation rather than OpenCL validation.

## Fusion Standalone validation

Repeat a representative TL/TC/TR render using a new node. Confirm CPU output
first. If Fusion advertises an OpenCL command queue, compare its output with
CPU and record the active backend. Do not add CUDA, a platform layout rule, or
an alternate CPU behavior to compensate for a host that does not expose
OpenCL.

## Windows-only fixes

Only modify `src/text_rasterizer_windows.cpp` if the full-resolution output
proves that DirectWrite produces an incomplete raster from a complete
`rendered_text` value. In that case, correct DirectWrite ink bounds and bitmap
allocation so Windows conforms to the shared contract. Do not change shared
anchoring geometry for a platform-specific font-metric difference.

## Return handoff

Commit all Windows source or test changes only on
`codex/windows-opencl-validation`. Add
`handoffs/windows-validation-results.md` containing:

- the exact updated base commit;
- Windows build and CTest results;
- Resolve and Fusion versions and contexts;
- CPU/OpenCL backend status;
- the requested log records and full-resolution still path;
- any Windows-only commit hashes;
- a clear pass/fail conclusion for complete text and invariant anchors.

Push the branch without merging it into `codex/v1-hardening`. macOS will review
and integrate the finished Windows commits afterward.
