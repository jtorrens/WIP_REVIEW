# Handoff — WIP Review OFX en Windows

Este handoff es exclusivamente para validar y corregir el OFX WIP Review en
Windows. ShotConfig e InputPrep son proyectos independientes: no se deben leer,
modificar, construir, probar ni integrar durante este trabajo.

## Prompt para Codex Windows

```text
Lee completamente WINDOWS_VALIDATION_HANDOFF.md y AGENTS.md antes de actuar.
Trabaja únicamente en la rama codex/windows-opencl-validation, basada en
origin/codex/v1-hardening. Compila, instala y valida WIP Review OFX en Resolve
y Fusion para Windows siguiendo todas las fases del handoff. La instalación
del bundle debe ser el último paso antes de cada ciclo de pruebas de host.
No mezcles la rama, no modifiques otras ramas y no añadas rutas legacy,
compatibilidad con contratos anteriores ni fallbacks sin especificación.
Avanza de forma autónoma hasta necesitar una comprobación visual del usuario.
Al terminar, publica la rama y entrega commits, resultados, logs resumidos y
el diff exacto para que el agente macOS haga la revisión e integración.
```

## Origen e aislamiento Git

Base de validación preparada en macOS:

- repositorio: `https://github.com/jtorrens/WIP_REVIEW.git`;
- rama base: `origin/codex/v1-hardening`;
- commit mínimo esperado: `0fe8d2c`;
- rama Windows: `codex/windows-opencl-validation`.

Usar un clon local nuevo, no una carpeta sincronizada mediante OneDrive,
Dropbox ni otro servicio de archivos:

```powershell
New-Item -ItemType Directory -Force C:\Codex | Out-Null
git clone https://github.com/jtorrens/WIP_REVIEW.git C:\Codex\WIP_REVIEW-Windows
Set-Location C:\Codex\WIP_REVIEW-Windows
git fetch origin --prune
git switch -c codex/windows-opencl-validation origin/codex/v1-hardening
git status --short --branch
git rev-parse HEAD
```

Si la rama remota ya existe, continuarla sin sobrescribirla:

```powershell
git switch --track origin/codex/windows-opencl-validation
```

Reglas Git:

- nunca trabajar directamente en `codex/v1-hardening` ni `main`;
- no hacer merge de ShotConfig, InputPrep ni ninguna rama ajena al OFX;
- no usar force-push;
- no añadir ningún contenido de `fusion/`;
- mantener commits pequeños y verificables;
- publicar únicamente `codex/windows-opencl-validation`;
- no fusionar la rama ni cerrar el PR de revisión;
- crear un PR draft hacia `codex/v1-hardening` solo si existe autorización y
  autenticación GitHub; si no, entregar únicamente la rama publicada.

## Contrato que se valida

Windows debe usar:

- OFX CPU cuando el host entrega punteros de memoria convencional;
- CUDA cuando el bundle fue compilado con `WIPREVIEW_ENABLE_CUDA=ON` y el host
  entrega un stream CUDA;
- OpenCL 1.1 cuando el host entrega buffers OpenCL;
- DirectWrite/GDI para rasterizar texto;
- bundle x64 en `Contents\Win64`.

No existe selector CPU por nodo. El host elige el tipo de buffer antes de
Render. Cada bundle anuncia únicamente los backends que contiene y no
interpreta handles CUDA/OpenCL como punteros CPU.

## Fase 1 — build limpio, sin hosts abiertos

Requisitos:

- Visual Studio 2022;
- workload `Desktop development with C++`;
- CMake 3.24 o posterior;
- Git;
- driver oficial de la GPU con `OpenCL.dll`.

Cerrar Resolve y Fusion antes de compilar el bundle que después se instalará.

```powershell
Set-Location C:\Codex\WIP_REVIEW-Windows
cmake -S . -B build-win -A x64 `
  -DCMAKE_BUILD_TYPE=Release `
  -DWIPREVIEW_BUILD_TESTS=ON `
  -DWIPREVIEW_BUILD_BENCHMARKS=OFF `
  -DWIPREVIEW_ENABLE_CUDA=OFF
cmake --build build-win --config Release --parallel
ctest --test-dir build-win -C Release --output-on-failure
cmake --build build-win --config Release --target package
```

Verificar:

- `build-win\WIPReviewProbe.ofx.bundle\Contents\Win64` contiene el binario;
- los exports son `OfxGetNumberOfPlugins`, `OfxGetPlugin` y `OfxSetHost`;
- no se distribuye una copia de `OpenCL.dll`;
- OpenCL-Headers procede del commit fijado en CMake;
- el paquete no contiene un segundo efecto Filter-only.

No instalar todavía si build, tests o empaquetado fallan.

## Fase 2 — preparar log e instalar al final

Crear una ruta de log explícita antes de abrir los hosts:

```powershell
New-Item -ItemType Directory -Force C:\Codex\WIPReviewLogs | Out-Null
[Environment]::SetEnvironmentVariable(
  "WIPREVIEW_PROBE_LOG",
  "C:\Codex\WIPReviewLogs\WIPReviewProbe.log",
  "User"
)
```

Confirmar mediante Task Manager que Resolve, Fusion y procesos auxiliares de
Fusion están cerrados. Después instalar desde PowerShell elevado. Esta
instalación es el último paso antes de abrir el host:

```powershell
cmake --install build-win --config Release `
  --prefix "C:\Program Files\Common Files\OFX\Plugins"
```

No modificar ni reconstruir el bundle entre esta instalación y el ciclo de
pruebas. Si se corrige código, cerrar ambos hosts, repetir build/tests y volver
a instalar únicamente como último paso del nuevo ciclo.

## Fase 3 — DaVinci Resolve Studio

Registrar versión exacta de Resolve, Windows, GPU y driver.

### Resolve Edit / OFX Filter

1. Usar un clip UHD o superior en timeline HD.
2. Confirmar que solo aparece `WIP Review`.
3. Aplicar el OFX como Filter.
4. Probar Identity, Fit, Fill/Crop, Stretch y 1:1.
5. Probar blanking 2.00 al 50 %, seis zonas, outline y shadow.
6. Probar Rec.709, PQ y HLG si el proyecto permite interpretar correctamente
   esas salidas.
7. Reproducir el caso semitransparente y registrar FPS sostenidos.

### Resolve Color / OFX Filter

Repetir el caso representativo en la página Color. Registrar bounds Source y
Output, renderWindow, renderScale y backend negociado.

El log debe indicar, para la ruta GPU:

```text
HOST_GPU_CAPABILITIES ... cuda="true" opencl_buffers="true"
RENDER_BACKEND ... cuda_enabled=1 opencl_enabled=0
GPU_RENDER ... backend=cuda status=0
```

No aceptar `RENDER_ERROR`, `kOfxStatGPURenderFailed`, imagen negra, crop
incorrecto, texto ausente ni diferencias de alpha como resultado válido.

## Fase 4 — Fusion Standalone

Ejecutar como mínimo:

1. Source no-HD `4608×3164`.
2. WIP Review en contexto General.
3. Requested Review Raster `1920×1080`.
4. `Settings → Use plugin RoD for output size` activado (`AllowResize=1`).
5. Los cinco placements y los tres filtros.
6. Blanking semitransparente, seis zonas, outline y shadow.
7. Rec.709, PQ y HLG.
8. Contexto Filter, si Fusion lo ofrece.

Confirmar que el raster de salida es realmente `1920×1080` y que Fit no se
convierte en crop. Si es posible, adaptar o crear un smoke PowerShell/Lua para
repetir la matriz sin intervención visual; no incorporar automatización que
dependa de rutas particulares del equipo.

## Fase 5 — comprobación CPU del host

Solo si puede hacerse sin alterar otros proyectos, desactivar temporalmente la
aceleración GPU desde la configuración global del host, reiniciar y verificar
una renderización CPU. El registro esperado es `cuda_enabled=0` y
`opencl_enabled=0`, sin `GPU_RENDER`. Restaurar después la configuración
original.

No crear un checkbox CPU dentro del OFX.

## Correcciones permitidas

Codex Windows puede modificar únicamente lo necesario para el OFX Windows:

- `CMakeLists.txt`;
- `.github/workflows/` si la corrección es estrictamente de CI Win64;
- `src/gpu_renderer_opencl.cpp`;
- `src/text_rasterizer_windows.cpp`;
- código host-neutral cuando un fallo reproducible demuestre que es necesario;
- tests y scripts Windows;
- `README.md`, `GPU_BACKEND_RESULTS.md` y un resultado Windows nuevo si aporta
  evidencia que no cabe claramente en el documento existente.

No modificar el backend Metal salvo que una corrección host-neutral lo exija y
quede explicada. No introducir código deprecado, parámetros antiguos, rutas
legacy ni compatibilidad con versiones anteriores.

## Evidencia y criterio de salida

Antes de publicar:

```powershell
git diff --check
git status --short
git diff --stat origin/codex/v1-hardening...HEAD
```

Buscar y eliminar dentro del scope cualquier etiqueta obsoleta, rama
inaccesible o contrato sustituido. No versionar builds, bundles instalados,
logs completos, cachés del host ni material de `fusion/`.

El handoff se considera completo cuando entrega:

- build MSVC x64 y CTest aprobados;
- bundle instalado y cargado por Resolve y Fusion;
- capacidades GPU exactas publicadas por ambos hosts;
- resultado OpenCL o bloqueo probado si el host no ofrece OpenCL buffers;
- matriz Resolve Edit, Resolve Color, Fusion General y Fusion Filter;
- FPS del caso con blanking semitransparente;
- rutas y commits modificados;
- rama publicada sin fusionar.

Formato de devolución al agente macOS:

```text
Branch:
Base commit:
Commits:
Changed paths:
Build/CTest/package:
Windows / GPU / driver:
Resolve version and results:
Fusion version and results:
OpenCL capability records:
Playback FPS:
Known limitations:
Draft PR URL (if any):
```

El agente macOS revisará el diff y decidirá merge o cherry-pick. Codex Windows
no debe integrar su propia rama en `codex/v1-hardening`.
