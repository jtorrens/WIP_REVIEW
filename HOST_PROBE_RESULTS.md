# Host Probe Results — WIP Review OFX P0

**Estado:** matriz crítica de raster completada en Resolve Edit/Color y Fusion General/Filter  
**Probe:** `WIPReviewProbe.ofx` 0.1.2  
**Requested physical raster:** `1920 × 1080`  
**Source objetivo de referencia:** `4608 × 3164`, PAR `1.0`  
**Source del probe en la primera medición:** `3840 × 2160`, PAR `1.0`, generado
upstream por `BetterResize` desde un Loader `1920 × 1080`

Este documento recoge observaciones empíricas. Un campo no medido permanece
`PENDING`; no se sustituye por una suposición basada en documentación o en el
comportamiento de otro contexto.

## Build probado

| Campo | Valor |
|---|---|
| Git commit del proyecto | El commit que contiene este informe (`git rev-parse HEAD`) |
| OpenFX SDK commit | `3de640d6f645fe6e346acd57e568d8b0a5ae4574` |
| Arquitectura binaria | Universal `arm64 + x86_64` |
| macOS | `15.6 (24G84)` |
| DaVinci Resolve/Fusion | Resolve `21.0.4.5`; Fusion Standalone `21.0.4.4` |
| Log original | `~/Library/Logs/WIPReviewProbe/WIPReviewProbe.log`; PIDs principales Resolve `22546`, Fusion `12664`, `21363`, `25805`, `26841` |

## Capacidades globales del host

Transcribir los eventos `HOST_*` y `SUITES` del log.

| Capacidad | Valor observado |
|---|---|
| Host name/version | `com.blackmagicdesign.Fusion`, `[21,0,4,4]` |
| OFX API version | Propiedad presente con dimensión vacía `[]` |
| Supported contexts | Fusion: General, Retimer, Transition, Filter, Generator; Resolve: Filter, General, Transition, Generator |
| Supports multi-resolution | `1` |
| Supports tiles | `0` |
| Supports multiple clip depths | `1` |
| Supports multiple clip PARs | `1` |
| Supports string animation | `1` |
| Colour-management style ofrecido | No publicado: `ErrUnknown` |
| Native configs ofrecidas | No publicadas: `ErrUnknown` |

## Matriz P0-Raster

Los bounds se anotan a `renderScale = [1,1]`. Si el host usa un origen distinto
de `[0,0]`, conservar el rect completo en lugar de anotar solo width/height.

Topología de la medición Fusion / General:

```text
Loader 1920×1080 (GlobalStart 24)
    → ColorSpaceTransform
    → BetterResize 3840×2160
    → WIPReviewProbe solicita Output 1920×1080
```

Por tanto, `Source 3840×2160` en el log es la entrada real del OFX, no la
resolución del archivo cargado. El test sigue siendo válido para custom Output
RoD porque el probe recibe un raster no-HD distinto del solicitado. Después se
repitió el caso crítico exacto cambiando `BetterResize` a `4608×3164` y
manteniendo el Output solicitado en `1920×1080`.

| Evidencia | Edit / Filter | Color / Filter | Fusion / General | Fusion / Filter |
|---|---|---|---|---|
| Context realmente instanciado | `OfxImageEffectContextFilter` | `OfxImageEffectContextFilter` | `OfxImageEffectContextGeneral` | `OfxImageEffectContextFilter` |
| Project size / extent / PAR | `[1920,1080] / [1920,1080] / 1` | `[1920,1080] / [1920,1080] / 1` | `[3840,2160] / [3840,2160] / 1` | `[3840,2160] / [3840,2160] / 1` |
| Source clip RoD | `[0,0,1920,1080]` | `[0,0,1920,1080]` | `[0,0,4608,3164]` en el caso crítico | `[0,0,4608,3164]` |
| Source image bounds / RoD | `[0,0,1920,1080]` | `[0,0,1920,1080]` a escala completa | `[0,0,4608,3164]` en el caso crítico | `[0,0,4608,3164]` |
| Output clip RoD | `[0,0,1920,1080]` | `[0,0,1920,1080]` | `[0,0,1920,1080]` con `AllowResize=1`; `[0,0,3840,2160]` por defecto | `[0,0,1920,1080]` con `AllowResize=1` |
| Output image bounds / RoD | `[0,0,1920,1080]` | `[0,0,1920,1080]` a escala completa | `[0,0,1920,1080]` con `AllowResize=1`; `[0,0,3840,2160]` por defecto | `[0,0,1920,1080]` con `AllowResize=1` |
| `renderWindow` | `[0,0,1920,1080]` | full `[0,0,1920,1080]`; proxy `[0,0,288,162]` observado | `[0,0,1920,1080]` con `AllowResize=1`; `[0,0,3840,2160]` por defecto | `[0,0,1920,1080]` |
| `renderScale` | `[1,1]` | full `[1,1]`; proxy `[0.15,0.15]` observado | `[1,1]` | `[1,1]` |
| Source RoI solicitado/respetado | Source ya conformado a timeline HD; Resolve no llama GetRoD/RoI | Igual; Resolve no llama GetRoD/RoI | Source completo `[0,0,4608,3164]` | Source completo `[0,0,4608,3164]` |
| Host entrega 1920×1080 físico | **Sí, también como Source** | **Sí, también como Source** | **Sí**, con `Settings → Use plugin RoD for output size` | **Sí**, con la misma opción |
| Resultado visual del probe | PENDING | PENDING | Crop 1:1 esperado: P0 copia coordenadas y no implementa resampling | Crop 1:1 esperado |
| Resultado provisional | **B: Resolve conforma antes del OFX** | **B: Resolve conforma antes del OFX** | **A viable**, condicionado a `AllowResize=1` y al renderer futuro | **A viable**, con la misma condición |

## PAR y premultiplicación

| Contexto/caso | Source PAR | Output PAR | Source premult | Output premult | Observación |
|---|---:|---:|---|---|---|
| Fusion 21 / General, UHD | 1 | 1 en imagen; clip Output no publica PAR | `OfxImageAlphaPremultiplied` | `OfxImageAlphaPremultiplied` | RGBA Half Float |
| Fusion 21 / Filter, Source PAR 2 | clip Source publica `0.5`; imagen Source publica `2` | se solicita `1` con status `OK`; clip Output no lo publica e imagen Output entrega `2` | `OfxImageAlphaPremultiplied` | `OfxImageAlphaPremultiplied` | Fusion no materializa el PAR Output solicitado y usa convenciones inconsistentes entre clip e imagen |

El RoD X está en coordenadas canónicas. Para un raster físico de width `W` y
PAR `p`, el probe solicita `x2 - x1 = W × p`; no interpretar el ancho canónico
como número de píxeles cuando `p != 1`.

En la prueba no cuadrada, `GetClipPreferences` escribió
`OfxImageClipPropPAR_Output=1.0` y Fusion devolvió `OK`. A continuación, el clip
Output siguió sin publicar PAR y la imagen Output llegó con PAR `2`. La
preferencia no se materializó. La ruta A queda validada para cambio de raster,
pero no para normalizar de forma fiable PAR no cuadrado a PAR 1 en Fusion 21.

## Multi-resolution / renderScale

| Contexto | Escala solicitada | Source bounds | Output bounds | renderWindow | Observación |
|---|---:|---|---|---|---|
| Fusion 21 / General, `AllowResize=0` | `[1,1]` | `[0,0,3840,2160]` | `[0,0,3840,2160]` | `[0,0,3840,2160]` | Ignora RoD pedido 1920×1080 |
| Fusion 21 / General, `AllowResize=1` | `[1,1]` | `[0,0,3840,2160]` | `[0,0,1920,1080]` | `[0,0,1920,1080]` | Materializa el Output RoD pedido |
| Fusion 21 / General, caso crítico | `[1,1]` | `[0,0,4608,3164]` | `[0,0,1920,1080]` | `[0,0,1920,1080]` | Source RoI solicitado completo; salida física HD confirmada |
| Fusion 21 / Filter, caso crítico | `[1,1]` | `[0,0,4608,3164]` | `[0,0,1920,1080]` | `[0,0,1920,1080]` | Descriptor Filter-only aceptado; salida física HD confirmada |
| Resolve Edit / Filter, clip UHD en timeline HD | `[1,1]` | `[0,0,1920,1080]` | `[0,0,1920,1080]` | `[0,0,1920,1080]` | Crop y Fit entregan el mismo raster HD al OFX; solo cambia el encuadre upstream |
| Resolve Color / Filter, full | `[1,1]` | `[0,0,1920,1080]` | `[0,0,1920,1080]` | `[0,0,1920,1080]` | Clip UHD ya conformado a la timeline antes del OFX |
| Resolve Color / Filter, proxy observado | `[0.15,0.15]` | `[0,0,288,162]` | `[0,0,288,162]` | `[0,0,288,162]` | Render proxy coherente con raster base 1920×1080 |
| Fusion 21 / General, ruta B | `[1,1]` | `[0,0,1920,1080]` | `[0,0,1920,1080]` | `[0,0,1920,1080]` | Host Raster conserva correctamente el raster upstream |

## Color OFX 1.5.1 y OCIO

El probe anuncia capacidad de negociación OCIO para descubrir el máximo común
con el host, y pide que Output use `OfxColourspace_Source`. No carga OCIO ni
aplica una transformación de color en P0.

| Contexto/proyecto | Estilo negociado | Native config | OCIO config/URI | Source colourspace | Output colourspace | Display / View |
|---|---|---|---|---|---|---|
| Fusion 21 / General | No publicado (`ErrUnknown`) | No publicada | No publicada | No publicado | No publicado | No publicados |
| Resolve Edit / Filter | `OfxImageEffectColourManagementFull` | `ofx-native-v1.5_aces-v1.3_ocio-v2.3` | No publicada | `Raw` | `OfxColourspace_Source` | No publicados |
| Resolve Color / Filter | `OfxImageEffectColourManagementFull` | `ofx-native-v1.5_aces-v1.3_ocio-v2.3` | No publicada | `Raw` | `OfxColourspace_Source` | No publicados |

## Coordenadas temporales y string animation

| Contexto | Tiempo OFX observado | Frame del clip/timeline | String isAnimating | Keys | Valor en ese tiempo |
|---|---:|---:|---|---:|---|
| Fusion 21 / General | `9` | Playhead observado `9` | `1` | `1` | `0` |
| Fusion 21 / General, Source inicia en 24 | `24` | Playhead `24`; primer frame de Source | `1` | `3` | `0` |
| Fusion 21 / General, Source inicia en 24 | `25` | Playhead `25`; segundo frame de Source | `1` | `3` | `0` |
| Fusion 21 / General | `45` | Playhead observado `45` | `1` | `2` | `1` |
| Fusion 21 / General | `82` | Playhead observado `82` | `1` | `3` | `3` |
| Resolve Color / Filter | `1212` | Antes de la segunda key observada | `0` | `2` | `A` |
| Resolve Color / Filter | `1219` | En/después de la segunda key observada | `0` | `2` | `B` |
| Resolve Color / Filter, clip inicia en timeline frame 100 | `0` | Primer frame del clip | `0` | `2` | `A` |
| Resolve Color / Filter, clip inicia en timeline frame 100 | `1` | Segundo frame del clip | `0` | `2` | `A` |

Anotar explícitamente si el tiempo observado es relativo al inicio del efecto,
al clip o al timeline. No inferirlo a partir de un único frame: usar al menos
dos frames consecutivos y un clip cuyo inicio no sea cero.

El Source se desplazó para comenzar en el frame de composición `24`. El primer
y segundo frame visibles llegaron al OFX como `time=24` y `time=25`, no como
`0/1`. En Fusion Standalone 21 / General, el tiempo OFX observado corresponde
al tiempo de composición/playhead, no al tiempo relativo al inicio del clip
fuente. La capacidad de animación de strings también queda confirmada: Fusion
devuelve los valores evaluados por tiempo y el número de keys correcto.

Resolve Color también crea y evalúa keys de string: el probe observó dos keys y
valores `A` y `B` a tiempos distintos. Sin embargo, la propiedad
`kOfxParamPropIsAnimating` permaneció en `0`. Para Resolve, el conteo de keys y
la evaluación por tiempo son evidencia positiva; `IsAnimating` no es fiable y
queda documentado como inconsistencia del host.

Para aislar la coordenada temporal, el clip se movió al frame `100` de la
timeline (`01:00:04:00` a 25 fps). Resolve Color entregó `time=0` y `time=1` en
los dos primeros frames del clip. Por tanto, en este contexto Resolve usa tiempo
relativo al clip, mientras Fusion General usa tiempo de composición/playhead.

## Evidencia visual/export

- Edit / Filter: observado interactivamente; captura no archivada
- Color / Filter: observado interactivamente; captura no archivada
- Fusion / General: observado interactivamente; crop 1:1 abajo a la izquierda
- Fusion / Filter: observado interactivamente; crop 1:1 abajo a la izquierda

## Decisión de arquitectura

### Criterio por contexto

Marcar `A` únicamente cuando Source sigue full-res, Output físico es
`1920 × 1080`, `renderWindow`/RoI son coherentes y viewer/export descartan que
el host imponga otro raster después del OFX. En cualquier otro caso marcar `B`.

| Contexto | A: Full-res → OFX → Review HD | B: Resize/Crop → OFX → Review HD | Evidencia decisiva |
|---|---|---|---|
| Resolve Edit / Filter | **No** | **Sí, interno al host** | Clip UHD, pero Source del OFX ya es 1920×1080 tanto con scaling Crop como Fit |
| Resolve Color / Filter | **No** | **Sí, interno al host** | Clip UHD, pero Source del OFX ya es 1920×1080; Resolve no llama GetRoD/RoI |
| Fusion / General | **Sí, condicionado** | Fallback validado | Source 4608×3164 y Output bounds/RoD/renderWindow 1920×1080 con `AllowResize=1` |
| Fusion / Filter | **Sí, condicionado** | Fallback no repetido | Context Filter real; Source 4608×3164 y Output/renderWindow 1920×1080 con `AllowResize=1` |

### Decisión final para el workflow

La arquitectura **A es viable en Fusion Standalone 21 / General**
si el nodo tiene activado `Settings → Use plugin RoD for output size`
(`AllowResize=1`). También es viable en el contexto Filter aislado. En Resolve
Edit y Color, en cambio, el OFX recibe el raster de timeline ya conformado: esos
contextos usan **B**, ejecutado internamente por Resolve antes del plugin.

El crop alineado abajo a la izquierda visto con el probe no invalida A: P0 no
contiene un resize. Su renderer
de prueba copia el píxel de Source con la misma coordenada de Output, por lo que
un Output menor muestra la esquina/intersección del Source. La medición relevante
es que Fusion entrega simultáneamente Source 4608×3164 y Output 1920×1080. El
renderer futuro deberá implementar explícitamente el muestreo y placement
full-res → review raster.

### Validación de la ruta B — Fusion Standalone 21 / General

Topología:

```text
Loader → ColorSpaceTransform → BetterResize 1920×1080
       → WIPReviewProbe con Canvas Mode = Host Raster
```

Observado repetidamente en los frames `30–35`:

```text
Source clip/image RoD y bounds = [0,0,1920,1080]
Output clip/image RoD y bounds = [0,0,1920,1080]
renderWindow                    = [0,0,1920,1080]
renderScale                     = [1,1]
PAR                             = 1
```

La ruta B queda validada para Fusion General: cuando el raster de review se
prepara upstream, el OFX conserva correctamente esa geometría.

## Workarounds específicos de Resolve/Fusion

| Workaround | Contexto/versión | Motivo | API pública insuficiente | Impacto / retirada |
|---|---|---|---|---|
| Activar `Use plugin RoD for output size` (`AllowResize=1`) | Fusion Standalone `21.0.4.4`, General y Filter | Fusion conserva por defecto el raster upstream aunque el OFX responda un RoD distinto | OpenFX permite declarar el RoD, pero el host controla el buffer y `renderWindow` | Requisito de configuración específico de Fusion; documentar en instalación/preset futuro |
| Normalizar PAR upstream antes del OFX | Fusion Standalone `21.0.4.4`, Filter | Fusion ignora en la imagen Output el PAR 1 solicitado por `GetClipPreferences` | El host devuelve `OK` al escribir la preferencia, pero entrega PAR 2 | Necesario solo para fuentes no cuadradas; revisar en futuras versiones del host |

## Contraste con especificación y documentación

- OpenFX permite que Source y Output tengan RoDs distintos cuando host y plugin
  soportan multirresolución, pero el host controla el buffer y no está obligado
  a renderizar todo el RoD: [OpenFX Image Processing Architectures](https://openfx.readthedocs.io/en/latest/Reference/ofxProcessingArch.html).
- RoD no equivale formalmente a un formato espacial persistente; OpenFX 1.x no
  ofrece una negociación de formato separada completa:
  [ASWF OpenFX issue 77 — Spatial Format](https://github.com/AcademySoftwareFoundation/openfx/issues/77).
- La opción específica de Fusion tiene un precedente público de plugin:
  [gyroflow-ofx v1.1](https://github.com/gyroflow/gyroflow-ofx/releases) documenta
  `Use plugin RoD for output size` para producir otro tamaño desde un nodo Fusion.
- Blackmagic documenta por separado DoD, OFX y los nodos nativos que cambian
  resolución, pero no garantiza el cambio de formato físico desde un OFX:
  [Fusion 21 Reference Manual](https://documents.blackmagicdesign.com/UserManuals/FusionManual.pdf).
