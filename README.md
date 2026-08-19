# WIP Review OFX — P0 Probe + P1 Formatter + P2c Drop Shadow

`WIPReviewProbe.ofx` conserva el probe P0 de capacidades OpenFX y añade el
checkpoint **P1a — Geometry/Placement**: canvas de review, colocación estática y
resampling CPU de referencia. P1b añade blanking editorial independiente, P2a
incorpora seis zonas simultáneas, P2b añade outline global y P2c añade drop
shadow global; ambos parten del alpha real de los glifos.

Este repositorio **no implementa** todavía tokens dinámicos, overflow,
transformaciones OCIO de píxeles, GPU ni presets de estado.
El probe anuncia y registra la
negociación de color OFX 1.5.1/OCIO, pero no transforma píxeles por color.

## P2c — Drop Shadow

P2c añade los controles globales **Shadow Enabled**, **Shadow Offset X/Y**,
**Shadow Softness**, **Shadow Colour** y **Shadow Opacity**. La sombra está
desactivada por defecto; sus valores candidatos son offset `0.0015, 0.0020`,
softness `0.0020`, negro y opacity `0.60`.

X se normaliza al ancho del Output y Y/softness a su altura; X positivo mueve
a la derecha y Y positivo visualmente hacia abajo. El renderer desplaza y
difumina la máscara alpha real antes de aplicar color, y compone en orden
`blanking → shadow → outline → fill`. Las tres capas comparten canvas y origen.
Véase [P2C_DROP_SHADOW_RESULTS.md](P2C_DROP_SHADOW_RESULTS.md).

## P2b — Outline

P2b añade los controles globales **Outline Enabled**, **Outline Width**,
**Outline Colour** y **Outline Opacity**. El default es negro opaco, activado,
con radio `0.0010` normalizado a la altura del Output.

El renderer dilata la máscara alfa real de cada glifo con un elemento circular.
No duplica el texto en varias direcciones. Fill y outline comparten un único
canvas expandido y una única ancla; el outline se compone primero y el fill
encima, después de placement y blanking. Véase
[P2B_OUTLINE_RESULTS.md](P2B_OUTLINE_RESULTS.md).

## P2a — Six Zones

P2a añade las zonas estáticas `TL`, `TC`, `TR`, `BL`, `BC` y `BR`. Cada zona
tiene enabled, string UTF-8, offsets normalizados y overrides opcionales de
tamaño, color y opacity. La alineación se deduce de la zona; fuente, estilo,
padding y valores base siguen siendo globales.

Las zonas se componen en orden `TL → TC → TR → BL → BC → BR` después del
blanking. El contrato es clean-forward: no conserva parámetros ni rutas de
render de versiones anteriores. En macOS el raster usa CoreText/CoreGraphics,
admite UTF-8 y cae a la fuente de sistema si la familia vigente no está
disponible. Véase
[P2A_SIX_ZONE_RESULTS.md](P2A_SIX_ZONE_RESULTS.md).

## P1b — Editorial Blanking

El blanking se compone después del placement y nunca cambia el raster ni recorta
físicamente la imagen. Controles:

- **Blanking Enabled**, desactivado por defecto;
- presets de display aspect `1.78`, `1.85`, `2.00`, `2.39` y **Custom**;
- color RGBA y opacity `0–1`.

La geometría incorpora Output PAR, genera letterbox o pillarbox sin asumir una
orientación fija y usa cobertura de píxel en límites fraccionales. La composición
se realiza en premult incluso cuando el Output negociado es straight-alpha. Véase
[P1B_BLANKING_RESULTS.md](P1B_BLANKING_RESULTS.md).

## P1a — Geometry/Placement

Controles implementados:

- **Canvas Mode**: Host Raster o Requested Review Raster;
- **Placement**: Identity, Fit, Fill / Crop, Stretch y 1:1;
- **Resample Filter**: Bilinear, Bicubic Catmull-Rom y Lanczos3;
- **Canvas Colour**: RGBA, negro opaco por defecto.

Fit y Fill calculan el aspect ratio de display incorporando Source/Output PAR.
Identity copia por coordenadas sin resize implícito y registra warning si los
rasters no coinciden. 1:1 centra píxeles físicos. El renderer limita toda
escritura al `renderWindow`, admite row bytes negativos y Byte/Short/Half/Float,
y filtra alpha en premult para evitar halos. La implementación y sus límites
están versionados en [P1A_GEOMETRY_RESULTS.md](P1A_GEOMETRY_RESULTS.md).

## Qué mide

El log registra, por acción e instancia:

- identidad, versión y versión de API del host;
- contextos y capacidades globales: multi-resolution, tiles, múltiples depths,
  múltiples PAR y animación de strings;
- tamaño, extent, offset, PAR y frame rate del proyecto;
- Source y Output: conexión, componentes, depth, PAR, frame range, frame rate,
  premultiplicación, colourspace y RoD;
- imágenes realmente entregadas: bounds, RoD, render scale, row bytes y formato;
- `renderWindow`, `renderScale`, tiempo, estado interactivo/secuencial;
- estilo de color negociado, config nativa, ruta/URI OCIO, display y view;
- valor del string animable en el tiempo, estado de animación y número de keys;
- RoD solicitado por el probe y Source RoI solicitado al host.

El control **Request Custom Output RoD** está activo por defecto con
`1920 × 1080`. Width representa píxeles físicos. El probe solicita Output PAR
`1.0` mediante `GetClipPreferences`; al calcular el RoD usa el PAR que publique
el clip Output y cae de forma explícita a `1.0` si el host no lo publica. El log
permite comparar esa negociación con el PAR de la imagen realmente entregada.

El bundle expone dos descriptores con el mismo renderer diagnóstico:

- `WIP Review Probe (P2c)`: anuncia Filter y General;
- `WIP Review Probe (P2c Filter Only)`: anuncia únicamente Filter para impedir
  que Fusion elija General durante la prueba comparativa.

## Dependencia OpenFX aislada

CMake descarga únicamente los headers del repositorio oficial ASWF OpenFX en
el directorio privado `_deps` del build. El commit está fijado a:

```text
3de640d6f645fe6e346acd57e568d8b0a5ae4574
```

No se incorporan al target la support library, ejemplos, tests ni dependencias
de terceros del SDK. Para un build offline puede proporcionarse un checkout
existente mediante `WIPREVIEW_OPENFX_SDK_ROOT`.

## Build macOS

Requisitos:

- Xcode Command Line Tools;
- CMake 3.24 o posterior;
- acceso a GitHub durante la primera configuración, salvo que se use un SDK
  local.

Build universal recomendado para Resolve/Fusion Intel y Apple Silicon:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Build offline con un checkout existente del SDK:

```sh
cmake -S . -B build \
  -DWIPREVIEW_OPENFX_SDK_ROOT=/ruta/al/openfx \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
```

El bundle queda en:

```text
build/WIPReviewProbe.ofx.bundle
```

Por defecto CMake aplica una firma ad-hoc al bundle para que el artefacto local
sea verificable por macOS. Puede desactivarse con
`-DWIPREVIEW_ADHOC_SIGN=OFF`; no es lógica ni un workaround del host.

Verificación rápida del artefacto:

```sh
file build/WIPReviewProbe.ofx.bundle/Contents/MacOS/WIPReviewProbe.ofx
codesign --verify --verbose=2 build/WIPReviewProbe.ofx.bundle
nm -gU build/WIPReviewProbe.ofx.bundle/Contents/MacOS/WIPReviewProbe.ofx \
  | grep -E 'Ofx(GetNumberOfPlugins|GetPlugin|SetHost)'
```

## Instalación macOS

Cierra Resolve/Fusion e instala para todos los usuarios:

```sh
sudo cmake --install build --prefix /Library/OFX/Plugins
```

El resultado debe ser:

```text
/Library/OFX/Plugins/WIPReviewProbe.ofx.bundle
```

Vuelve a abrir Resolve/Fusion. Los dos descriptores aparecen bajo
`WIP Review/Diagnostics`. Tras reemplazar un build, reinicia el host para evitar
que conserve el binario anterior en memoria.

## Log

Ruta por defecto:

```text
~/Library/Logs/WIPReviewProbe/WIPReviewProbe.log
```

Cada línea contiene timestamp, PID, evento y pares `clave=valor`. Los eventos
`INSTANCE_CREATE`, `GET_REGION_OF_DEFINITION`, `GET_REGIONS_OF_INTEREST`,
`RENDER`, `CLIP`, `IMAGE`, `INSTANCE_COLOUR_NEGOTIATION`,
`TEMPORAL_STRING_PROBE`, `STATIC_FORMATTER`, `EDITORIAL_BLANKING`,
`TEXT_OUTLINE`, `TEXT_SHADOW` y `TEXT_ZONE` forman el registro tipográfico
principal.

Para usar otra ruta, inicia el host desde un entorno que contenga:

```sh
export WIPREVIEW_PROBE_LOG=/ruta/escribible/WIPReviewProbe.log
```

El logger usa append, está protegido para renders concurrentes y nunca eleva
un error de I/O al host.

## Test automático en Fusion Standalone

El harness macOS crea una composición privada y genera un Source `4608×3164`.
Prueba los cinco placements en Filter-only con Output `1920×1080`, repite Fit en
General y valida además `Canvas Mode = Host Raster` con Output `4608×3164`.
Activa `AllowResize`, fuerza cada render y valida exclusivamente el tramo nuevo
del log. La composición temporal se cierra bloqueada para que Fusion no muestre
un diálogo de guardado; la composición que estuviera activa se restaura y nunca
se modifica.

P1b añade los escenarios B01–B04: aspect 2.00, opacity 0.5, blanking off y
pillarbox Custom 1.33. El mismo runner mantiene toda la cobertura P1a.

La cobertura tipográfica valida UTF-8, crecimiento desde anclajes
superior/inferior, fuente ausente, texto sobre blanking, seis zonas simultáneas,
overrides, offsets, outline y shadow con geometría/color/opacity independientes
del fill.

Con el bundle ya instalado:

```sh
scripts/run_fusion_smoke.sh
```

También está disponible como target explícito, fuera de `ctest` porque necesita
la aplicación gráfica instalada:

```sh
cmake --build build --target fusion_host_smoke
```

El script abre Fusion si no está ejecutándose y conecta mediante el `fuscript`
incluido en Fusion 21. Un crash, bloqueo de licencia o diálogo excepcional del
sistema sigue siendo una condición externa al harness.

### Composición de validación visual

Con `ffmpeg` disponible, este comando genera una carta temporal `4608×3164` y
deja abierta una composición aislada con Identity, Fit, Fill/Crop, Stretch,
1:1, Host Raster, los casos de blanking y la matriz tipográfica P2c:

```sh
scripts/open_fusion_visual.sh
```

Selecciona cada nodo `GEOMETRY_*`, `BLANKING_*`, `P2A_*`, `P2B_*` o `P2C_*` y
pulsa `1` o `2`.
La composición se llama `WIPReview_VISUAL_VALIDATION_DO_NOT_SAVE`; es
intencionadamente temporal y no modifica la composición que estuviera activa.

## Protocolo P0-Raster obligatorio

1. Usa una entrada real `4608 × 3164` con PAR 1.0. Evita que un nodo upstream
   la reduzca sin registrarlo.
2. Mantén `Request Custom Output RoD = On`, Width `1920`, Height `1080`.
3. Asigna una etiqueta única en **Scenario Label** antes de renderizar.
4. Fuerza al menos un render completo a escala 1.0 y guarda captura/export para
   detectar un resize posterior al OFX que el API no revela.
5. Repite separadamente:

   - `Resolve Edit / OFX Filter`;
   - `Resolve Color / OFX Filter`;
   - `Fusion / OFX General`;
   - `Fusion / OFX Filter`, solo si el host lo ofrece.

   En Fusion activa en la pestaña común `Settings` del nodo
   **Use plugin RoD for output size** (`AllowResize=1`). Sin esa opción Fusion
   conserva el raster upstream aunque llame a `GetRegionOfDefinition`.

6. En **Animated String Probe**, crea dos keys con textos distintos y renderiza
   ambos frames. El log debe mostrar `is_animating`, `key_count` y el valor
   devuelto para cada tiempo.
7. Repite un frame con proxy/render scale distinto de 1.0.
8. Para PAR/premultiplicación, añade pruebas separadas con media anamórfica y
   RGBA premultiplicado/no premultiplicado; no mezcles esos resultados con el
   caso raster base.
9. Para color, repite en la configuración de gestión de color que vaya a usar
   producción y registra el estilo/config que negocia el host.
10. Transcribe las evidencias a [HOST_PROBE_RESULTS.md](HOST_PROBE_RESULTS.md).

No hay detección basada en nombre de host ni rutas privadas de Resolve. El mismo
binario ejecuta el contrato OpenFX publicado en todos los contextos. Las opciones
o limitaciones específicas del host descubiertas durante P0 se documentan en
`HOST_PROBE_RESULTS.md`; no se activan silenciosamente desde el plugin.

## Regla de decisión A/B

Un contexto habilita **A) Full-res → OFX → Review Raster HD** solo si a escala
1.0 se cumplen simultáneamente:

- Source conserva el RoD/bounds full-res esperado;
- Output RoD y bounds físicos son `1920 × 1080`;
- `renderWindow` es coherente con ese output;
- el Source RoI permite acceder a la imagen necesaria;
- viewer y export confirman que el host no vuelve a imponer el project extent
  después del efecto.

Si falla cualquiera de esas condiciones, ese contexto usa
**B) Full-res → Crop/Resize → OFX → Review HD**. La decisión se toma por
contexto; no se extrapola un resultado de Fusion General a Edit/Color Filter.

## Resultado P0 medido

- Fusion Standalone 21 General y Filter permiten **A**, condicionado a
  `Use plugin RoD for output size`. Source `4608×3164` y Output físico
  `1920×1080` fueron observados simultáneamente.
- Resolve 21 Edit y Color usan **B** internamente: con clip UHD en timeline HD,
  el Source entregado al OFX ya es `1920×1080`.
- Fusion no materializó Output PAR `1` desde Source PAR `2`, aunque aceptó la
  preferencia con status `OK`; una fuente no cuadrada debe normalizarse upstream.

La evidencia completa y versionada está en
[HOST_PROBE_RESULTS.md](HOST_PROBE_RESULTS.md).
