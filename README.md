# WIP Review OFX — P0 Host Probe

`WIPReviewProbe.ofx` es el probe P0 de capacidades OpenFX para DaVinci
Resolve/Fusion. Su única función es medir el contrato real del host antes de
congelar la arquitectura del renderer de WIP Review.

Este repositorio **no implementa** el formatter final, seis zonas, tipografía,
blanking, transformaciones OCIO, GPU ni presets. El probe anuncia y registra la
negociación de color OFX 1.5.1/OCIO, pero no transforma píxeles por color. Para
mantener una salida válida, limpia el `renderWindow` y copia solo la intersección
Source/Output con coordenadas idénticas; nunca hace resize ni reframe.

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

- `WIP Review Probe (P0)`: anuncia Filter y General;
- `WIP Review Probe (P0 Filter Only)`: anuncia únicamente Filter para impedir
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
`RENDER`, `CLIP`, `IMAGE`, `INSTANCE_COLOUR_NEGOTIATION` y
`TEMPORAL_STRING_PROBE` forman el registro principal.

Para usar otra ruta, inicia el host desde un entorno que contenga:

```sh
export WIPREVIEW_PROBE_LOG=/ruta/escribible/WIPReviewProbe.log
```

El logger usa append, está protegido para renders concurrentes y nunca eleva
un error de I/O al host.

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
