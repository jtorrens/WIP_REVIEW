# V1 — Matriz de aceptación

Esta matriz separa pruebas host-independent, evidencia ya obtenida en host y el
último ciclo que debe repetirse con el bundle final. La fuente de verdad es
`WIP_Review_OFX_V1.1_Spec.md`, sección 31.

## Raster / Host

| ID | Estado | Evidencia |
|---|---|---|
| R01 Requested Review Raster | Validado con bundle final | Fusion 21 General y Filter entregaron simultáneamente Source 4608×3164 y Output 1920×1080 con `AllowResize=1`. El smoke comprueba RoD, bounds, renderWindow, RoI y placements. |
| R02 Unsupported Requested Raster | Implementado; repetir en Resolve final | La matriz de host habilita Requested solo para `com.blackmagicdesign.Fusion`. Resolve y hosts desconocidos bloquean Canvas Mode en Host Raster; la lectura de parámetros vuelve a comprobar la capacidad antes de solicitar RoD. |
| R03 Upstream equivalence | Automatizado | `wipreview_v1_acceptance` compara la ruta integrada full-res→HD con placement upstream equivalente, y comprueba que Host Raster/Identity no altera el resultado. |
| R04 Master isolation | Responsabilidad de composición | El OFX no modifica ramas no conectadas. La aceptación operativa exige una rama de conform nativa separada de la rama de review; se comprueba en el proyecto de entrega, no dentro del renderer. |

Conclusión por host:

- Fusion General/Filter: ruta A, condicionada a `Use plugin RoD for output size`.
- Resolve Edit/Color: ruta B; el OFX recibe el raster de timeline.
- PAR no cuadrado en Fusion 21: normalización upstream documentada.

Detalle: [HOST_PROBE_RESULTS.md](HOST_PROBE_RESULTS.md).

## Geometry y placement

| ID | Estado | Evidencia |
|---|---|---|
| G01 HD→UHD | Automatizado y visual | Igual composición normalizada, aperture, padding y escala relativa a 1920×1080 y 3840×2160. |
| G02 UHD→DCI | Automatizado | 3840×2160 y 4096×2160 producen el mismo tamaño de fuente en píxeles; las celdas y paddings horizontales siguen el ancho. |
| G03 PAR | Automatizado | Fit usa display aspect con PAR anamórfico; blanking incorpora Output PAR. |
| P01 Fit | Automatizado y visual | Fuente completa, aspect preservado. |
| P02 Fill | Automatizado y visual | Canvas cubierto con crop centrado y aspect preservado. |
| P03 Stretch | Automatizado y visual | Canvas cubierto con deformación intencional. |
| P04 1:1 | Automatizado y visual | Mapeo físico centrado, distinto de Identity por coordenadas. |

## Blanking, tipografía y dinámica

| Grupo | Estado | Cobertura |
|---|---|---|
| B01–B04 | Automatizado y visual | Letterbox 2.00, opacity 0.5, Off y pillarbox Custom. |
| T01–T06 | Automatizado y visual | Padding independiente, crecimiento top/bottom, outline, shadow, UTF-8 y overflow por celda. |
| D01–D03 | Automatizado y visual | `{frame_rel}`, `{frame}` y timecode a 24, 25, 30, 23.976 y 29.97 DF. |

Las evidencias detalladas están en los documentos P1B, P2A–P2D y P3.

## Color, alpha y depth

| ID | Estado | Evidencia |
|---|---|---|
| C01 Input ACEScg | Contrato y visual | CST nativo upstream ACEScg→espacio display-referred; el OFX no duplica la transformación fotográfica. |
| C02 Input Rec.709 | Automatizado y visual | Rec.709 se interpreta como display-referred, no scene-linear. |
| C03 Text antialias | Automatizado | Máscaras de glifo se componen en luz lineal de display. |
| C04 PQ Graphics White | Automatizado y visual | Picker 1.0 alcanza Graphics White configurado, no peak display. |
| C05 Blanking opacity | Automatizado | La mezcla al 50 % se realiza después de decode, en display-light linear. |
| C06 Unaffected picture | Automatizado | Round-trip identity Rec.709/PQ/HLG dentro de tolerancia por encoding/depth. |
| Alpha | Automatizado | Straight/premult, alpha cero, filtrado premultiplicado y row bytes negativos. |
| Depth | Automatizado | Byte, Short, Half y Float, incluidas conversiones y tolerancias cuantizadas. |

Detalle: [P4_MANAGED_COLOR_RESULTS.md](P4_MANAGED_COLOR_RESULTS.md).

## Comandos reproducibles

```sh
cmake -S . -B build-v1 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-v1 --parallel
ctest --test-dir build-v1 --output-on-failure
```

Con el bundle instalado y Fusion cerrado antes de la instalación:

```sh
scripts/run_fusion_smoke.sh
scripts/open_fusion_visual.sh
```

## Estado del artefacto final

Validado el 20 de agosto de 2026 en Fusion Studio 21.0.4 con el binario del
commit `7d47575`:

- smoke acumulativo del descriptor único: 35 renders aprobados;
- descriptor único en Fusion General: Requested 1920×1080 desde Source
  4608×3164;
- Host Raster y los cinco placements aprobados;
- blanking, seis zonas, outline, shadow, overflow y tokens aprobados;
- Rec.709, PQ y HLG aprobados;
- firma ad-hoc, símbolos OFX y arquitecturas arm64/x86_64 verificados.

Paquete:

```text
build-universal/WIPReviewProbe-1.1.0-macOS-universal.zip
SHA-256 918e47b835282b8556de344a199ea338d143d3c6ffffc9d424ad0a6ae1b18621
```

Antes del tag V1 queda repetir la carga en Resolve Edit y Color con este mismo
bundle, confirmando Canvas Mode bloqueado en Host Raster y Source/Output de
timeline. La comprobación visual HD/UHD/DCI ya fue realizada durante las fases
de geometría y color y queda cubierta adicionalmente por la aceptación local.
