# Handoff macOS ↔ Windows — integración CUDA

Fecha: 2026-08-21

## Estado entregado

- Rama publicada: `codex/windows-opencl-validation`.
- Base de integración: `origin/codex/v1-hardening`.
- Último commit: `13be1d6` (`docs: record Windows CUDA validation`).
- La rama está limpia y no se ha fusionado ni reescrito.
- No se han leído ni modificado `ShotConfig`, `InputPrep` ni `fusion/`.

## Validación completada

- CI verde: https://github.com/jtorrens/WIP_REVIEW/actions/runs/32467058255.
- macOS y Windows CUDA completaron configure, MSVC x64 build, CTest, CPack y
  carga del bundle.
- En Resolve sobre Windows, el log confirmó `cuda_enabled=1` y
  `GPU_RENDER backend=cuda status=0`.
- El clip UHD con WIP Review mantuvo 25 fps en la página Color.
- La corrección de empaquetado sitúa el binario Windows directamente en
  `Contents/Win64/WIPReviewProbe.ofx`; Resolve no carga el subdirectorio
  `Release` de los generadores multi-config.

## Cambios a revisar

- `src/probe.cpp`: anuncia y negocia CUDA stream solo cuando Resolve lo ofrece;
  conserva OpenCL únicamente para la ruta que entrega cola OpenCL.
- `src/gpu_renderer_cuda.cu` y `tests/gpu_renderer_cuda_tests.cpp`: backend y
  pruebas CUDA de Windows.
- `CMakeLists.txt`: salida de cada configuración Windows al directorio que el
  host carga.
- `.github/workflows/build.yml`: trabajo Windows CUDA con runner etiquetado.
- `handoffs/windows-validation-results.md`: evidencia y hash del binario
  instalado.

## Próximo paso para macOS

1. Revisar el diff `origin/codex/v1-hardening...codex/windows-opencl-validation`.
2. Proveer un runner Windows `self-hosted`, `Windows`, `X64`, `cuda` antes de
   requerir CI CUDA en nuevos pushes; el runner temporal fue desregistrado y su
   carpeta local eliminada tras la validación.
3. Abrir y revisar un PR hacia `codex/v1-hardening`; no fusionar directamente
   a `main`.
4. Tras integrar, ejecutar CI con ese runner y conservar la prueba visual de
   Resolve como validación de host, no como sustituto de CTest.
