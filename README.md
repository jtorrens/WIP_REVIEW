# WIP Review OFX — V1

`WIPReviewProbe.ofx` es un efecto OpenFX de review para DaVinci Resolve y
Fusion. Coloca una imagen en un raster de review, aplica blanking editorial y
compone seis zonas de texto mediante Metal en macOS, OpenCL en Windows o CPU
cuando el host entrega memoria convencional. No incluye una transformación
fotográfica interna. Los presets disponibles se limitan a
rasters y aspectos técnicos; no codifican semántica de producción.

La imagen fotográfica debe llegar ya transformada al espacio display-referred
de review mediante el Color Space Transform nativo del host. El plugin recibe y
devuelve el mismo espacio, decodifica Rec.709/PQ/HLG a luz lineal de display,
compone los gráficos y codifica una sola vez.

## Contrato de raster

**Canvas Mode** es la única selección de raster:

- **Requested Review Raster** ofrece HD, UHD, DCI 2K, DCI 4K y Custom. Está
  habilitado y seleccionado por defecto únicamente en Fusion, cuya ruta fue validada con
  `Settings → Use plugin RoD for output size` (`AllowResize=1`).
- **Host Raster** conserva el raster publicado por el host. Es el único modo
  disponible en Resolve y en hosts no validados.

Esto implementa la matriz medida, sin rutas privadas del host:

- Fusion General/Filter permite **A) Full-res → OFX → Review Raster**, con
  `Use plugin RoD for output size` activo.
- Resolve Edit/Color entrega al OFX el raster ya conformado a timeline y usa
  **B) Full-res → Crop/Resize → OFX → Review HD**.
- Una fuente con PAR no cuadrado se normaliza upstream en Fusion 21 porque el
  host no materializa de forma consistente el Output PAR solicitado.

La evidencia está en [HOST_PROBE_RESULTS.md](HOST_PROBE_RESULTS.md).

## Inspector

El contrato se organiza en seis páginas OFX: **Processing**, **Canvas**,
**Typography**, **Zones**, **Timing** y **Color**. Processing muestra las
capacidades GPU negociadas con el host. Los mismos bloques existen como
jerarquía de grupos para hosts que presentan un inspector plegable en vez de
páginas.

Los controles dependientes se deshabilitan cuando no tienen efecto: raster
Custom, aspect Custom, blanking, outline, shadow, overrides por zona, FPS
Override y Graphics White manual. El estado de capacidad de raster se muestra
como información no editable. Dentro de cada zona las etiquetas se acortan a
`Enabled`, `Text`, offsets y overrides porque el grupo ya identifica la zona.
La matriz completa está en [INSPECTOR_UX.md](INSPECTOR_UX.md).

La negociación Metal/CUDA/OpenCL y la base de empaquetado Windows se documentan
en [GPU_BACKEND_RESULTS.md](GPU_BACKEND_RESULTS.md).

La ruta Host Raster 1:1 compone el blanking mediante una pasada fusionada y
multihilo directamente sobre Output. Las intersecciones tipográficas comparten
un único workspace y encode final, y las máscaras sin cambios se reutilizan.
Las mediciones reproducibles y la comparación con el pipeline completo están
en [PERFORMANCE_RESULTS.md](PERFORMANCE_RESULTS.md).

## Geometry/Placement

- **Placement**: Identity, Fit, Fill / Crop, Stretch y 1:1.
- **Resample Filter**: Bilinear, Bicubic Catmull-Rom y Lanczos3.
- **Canvas Color**: RGBA, negro opaco por defecto.

Fit y Fill incorporan Source/Output PAR. Identity alinea coordenadas sin resize
implícito; 1:1 centra píxeles físicos. El renderer limita las escrituras al
`renderWindow`, admite row bytes negativos y Byte/Short/Half/Float, y filtra
alpha en premult para evitar halos. Detalles en
[P1A_GEOMETRY_RESULTS.md](P1A_GEOMETRY_RESULTS.md).

## Editorial Blanking

El blanking se compone después del placement y no cambia el raster. Ofrece
presets 1.78, 1.85, 2.00, 2.39 y Custom, además de color y opacidad. Su
geometría incorpora Output PAR y genera letterbox o pillarbox según corresponda.
Véase [P1B_BLANKING_RESULTS.md](P1B_BLANKING_RESULTS.md).

## Seis zonas y tipografía

Las zonas `TL`, `TC`, `TR`, `BL`, `BC` y `BR` tienen enabled, string UTF-8,
offsets y overrides opcionales de tamaño, color y opacidad. Fuente, estilo,
padding y valores base son globales. CoreText/CoreGraphics rasteriza el texto
en macOS; si la familia vigente no existe, usa la fuente de sistema.

El orden de composición es `blanking → shadow → outline → fill`. Outline usa
dilatación de la máscara real del glifo. Shadow desplaza y difumina esa máscara.
Cada zona está limitada a su celda lógica mediante Clip, Ellipsis o
ShrinkToFit. Detalles:

- [P2A_SIX_ZONE_RESULTS.md](P2A_SIX_ZONE_RESULTS.md)
- [P2B_OUTLINE_RESULTS.md](P2B_OUTLINE_RESULTS.md)
- [P2C_DROP_SHADOW_RESULTS.md](P2C_DROP_SHADOW_RESULTS.md)
- [P2D_OVERFLOW_RESULTS.md](P2D_OVERFLOW_RESULTS.md)

## Campos calculados

Cada zona ofrece **Text** como prefijo literal y, a continuación,
**Calculated Field** con None, Frame Relative, Frame, Timecode o Date. Resolve
añade Source Frame y Source Filename cuando carga el plugin; Fusion conserva
solo el conjunto portable. Frame Start, Timecode Start, Review Date y los
ajustes de FPS son globales. Véase
[P3_CALCULATED_FIELDS_RESULTS.md](P3_CALCULATED_FIELDS_RESULTS.md).

## Color gestionado

Input y Output usan el mismo espacio display-referred, seleccionado mediante
**Auto from Host** o **Manual Override**. Manual admite Rec.709 Gamma 2.4,
Rec.2100 PQ y Rec.2100 HLG. `Raw`, vacío o un nombre desconocido requieren la
interpretación manual y generan un warning visible.

Graphics White automático usa 100 nits en SDR, 203 nits en PQ y el 20,3 % del
peak HLG. La transformación de cámara/scene/ACEScg hacia ese espacio se hace
upstream con el CST nativo de Fusion/Resolve. No hay una configuración OCIO
paralela dentro del plugin. Véase
[P4_MANAGED_COLOR_RESULTS.md](P4_MANAGED_COLOR_RESULTS.md).

## Registro de capacidades y render

El log registra identidad/API del host, contextos, multi-resolution, tiles,
depths, PAR, Source/Output, bounds y RoD entregados, `renderWindow`,
`renderScale`, premultiplicación, colourspace y negociación de color. También
registra placement, blanking, capas tipográficas, campos calculados y uso de la ruta
multithread.

Ruta por defecto:

```text
~/Library/Logs/WIPReviewProbe/WIPReviewProbe.log
```

Puede cambiarse antes de iniciar el host:

```sh
export WIPREVIEW_PROBE_LOG=/ruta/escribible/WIPReviewProbe.log
```

Los eventos principales son `INSTANCE_CREATE`,
`GET_REGION_OF_DEFINITION`, `GET_REGIONS_OF_INTEREST`, `RENDER`, `CLIP`,
`IMAGE`, `INSTANCE_COLOUR_NEGOTIATION`, `STATIC_FORMATTER`,
`EDITORIAL_BLANKING`, `TEXT_OUTLINE`, `TEXT_SHADOW`, `TEXT_OVERFLOW`,
`CALCULATED_FIELDS`, `CALCULATED_FIELD_ZONE` y `TEXT_ZONE`.

## Dependencia OpenFX aislada

CMake usa solo los headers del SDK oficial ASWF OpenFX. El checkout vive en
`_deps` dentro del build y está fijado al commit:

```text
3de640d6f645fe6e346acd57e568d8b0a5ae4574
```

No se enlazan support library, ejemplos, tests ni dependencias de terceros. Un
checkout existente puede indicarse con `WIPREVIEW_OPENFX_SDK_ROOT`.

## Build macOS

Requisitos: Xcode Command Line Tools y CMake 3.24 o posterior.

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Build offline:

```sh
cmake -S . -B build \
  -DWIPREVIEW_OPENFX_SDK_ROOT=/ruta/al/openfx \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
```

El artefacto queda en `build/WIPReviewProbe.ofx.bundle`. CMake aplica una firma
ad-hoc por defecto; puede desactivarse con `-DWIPREVIEW_ADHOC_SIGN=OFF`.

Verificación rápida:

```sh
file build/WIPReviewProbe.ofx.bundle/Contents/MacOS/WIPReviewProbe.ofx
codesign --verify --verbose=2 build/WIPReviewProbe.ofx.bundle
nm -gU build/WIPReviewProbe.ofx.bundle/Contents/MacOS/WIPReviewProbe.ofx \
  | grep -E 'Ofx(GetNumberOfPlugins|GetPlugin|SetHost)'
```

## Instalación macOS

Cierra Resolve y Fusion. La instalación para todos los usuarios es:

```sh
sudo cmake --install build --prefix /Library/OFX/Plugins
```

El resultado es
`/Library/OFX/Plugins/WIPReviewProbe.ofx.bundle`. Reinicia el host después de
reemplazar el bundle. El único efecto aparece en `WIP Review` como
`WIP Review`; el mismo descriptor declara Filter y General.

## Build e instalación Windows x64

Requisitos: Visual Studio 2022 con Desktop development with C++, CMake 3.24 o
posterior y un driver GPU que instale `OpenCL.dll` para ejecutar la ruta GPU.

```powershell
cmake -S . -B build-win -A x64 `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-win --config Release --parallel
ctest --test-dir build-win -C Release --output-on-failure
cmake --install build-win --config Release `
  --prefix "C:\Program Files\Common Files\OFX\Plugins"
```

El binario queda dentro del bundle en `Contents\Win64`. OpenCL se resuelve
contra el ICD del sistema en tiempo de ejecución; el paquete no incluye una
copia de `OpenCL.dll`.

La validación en una máquina Windows debe hacerse desde una rama aislada. El
procedimiento completo para Codex Windows está en
[WINDOWS_VALIDATION_HANDOFF.md](WINDOWS_VALIDATION_HANDOFF.md).

## Validación automática en Fusion Standalone

Con el bundle instalado:

```sh
scripts/run_fusion_smoke.sh
```

El harness crea una composición privada con Source `4608×3164`, valida los
cinco placements en Output `1920×1080`, Host Raster, blanking, seis zonas,
outline, shadow, overflow, campos calculados y Rec.709/PQ/HLG sobre Metal. La
composición activa se restaura y no se modifica.

También puede ejecutarse mediante:

```sh
cmake --build build --target fusion_host_smoke
```

Para chequeo visual:

```sh
scripts/open_fusion_visual.sh
```

Selecciona nodos `GEOMETRY_*`, `BLANKING_*`, `P2A_*`, `P2B_*`, `P2C_*`,
`P2D_*`, `P3_*` o `P4_*` y pulsa `1` o `2`. Rec.709 se visualiza directamente.
Para PQ/HLG en un viewer SDR, conecta después un CST nativo hacia Rec.709 con
`HDR 203 Nits Diffuse White` activo.

## Benchmark CPU

El benchmark es opt-in:

```sh
cmake -S . -B build-perf \
  -DCMAKE_BUILD_TYPE=Release \
  -DWIPREVIEW_BUILD_BENCHMARKS=ON \
  -DWIPREVIEW_OPENFX_SDK_ROOT=/ruta/al/openfx-fijado
cmake --build build-perf --target wipreview_cpu_benchmark
./build-perf/wipreview_cpu_benchmark \
  --case fullres_to_hd --encoding all --threads 1
```

Casos: `equivalence_probe`, `fullres_to_hd`, `uhd_identity` y `dci_fit`.
Resultados en [P5_PERFORMANCE_RESULTS.md](P5_PERFORMANCE_RESULTS.md).

## Aceptación V1

La matriz formal de HD/UHD/DCI, PAR, rutas A/B, Identity, alpha, depths, color y
host está en [V1_ACCEPTANCE_RESULTS.md](V1_ACCEPTANCE_RESULTS.md).
