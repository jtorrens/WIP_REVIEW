# Windows validation results

Date: 2026-08-21

## Environment

- Branch: `codex/windows-opencl-validation`
- Commit: `f78b7db`
- GPU: NVIDIA GeForce RTX 4080 Laptop GPU
- CUDA Toolkit: 13.3
- Host: DaVinci Resolve, `OfxImageEffectContextGeneral`

## Automated validation

GitHub Actions run `32466387101` passed on macOS and on the temporary Windows
CUDA runner:

- MSVC x64 configure and build;
- CTest, including the CUDA renderer test;
- CPack ZIP package;
- uploaded Windows OFX bundle and package.

## Manual Resolve validation

- The installed active binary hash is
  `D0EAB14108F73B23A916F6F115DD284EEA9B3EA78404550781A4F074DDF702AD`.
- Resolve negotiated CUDA: `cuda_enabled=1`, `opencl_enabled=0`.
- The renderer logged `GPU_RENDER backend=cuda status=0`.
- The tested UHD clip with WIP Review maintained 25 fps.

## Packaging correction

Windows multi-config CMake generators had placed the binary at
`Contents/Win64/Release`, while Resolve loads
`Contents/Win64/WIPReviewProbe.ofx`. The package now fixes every Windows
configuration output directory to the host bundle path.
