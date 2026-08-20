# P5 Performance — renderer CPU

**Rama:** `p5-performance`  
**Estado:** aprobado en Fusion Standalone 21.0.4.

## Contrato

P5 no modifica parámetros OFX, espacios de color, geometría ni orden de
composición. La prioridad es conservar el resultado P4 y medir antes de elegir
SIMD, threading o GPU.

El benchmark es host-independent y opt-in:

```sh
cmake -S . -B build-p5 \
  -DCMAKE_BUILD_TYPE=Release \
  -DWIPREVIEW_BUILD_BENCHMARKS=ON \
  -DWIPREVIEW_OPENFX_SDK_ROOT=/ruta/al/openfx-fijado
cmake --build build-p5 --target wipreview_cpu_benchmark
./build-p5/wipreview_cpu_benchmark \
  --case fullres_to_hd --encoding all --threads 1
```

No forma parte de `ctest` ni de builds normales. Procesa una fuente RGBA float
premultiplicada determinista, Lanczos3, blanking, seis máscaras de texto y el
encode final. Los tiempos separan:

- resampling + decode display-referred;
- blanking + seis overlays;
- encode final.

## Entorno del baseline

- Apple M3 Ultra, arm64;
- macOS 15.6 (`24G84`);
- AppleClang 17.0.0;
- Release, proceso CPU single-threaded.

## Resultados preliminares

### Fuente 4608×3164 → review 1920×1080

| Encoding | Render/decode | Overlays | Encode | Total |
| --- | ---: | ---: | ---: | ---: |
| Rec.709 Gamma 2.4 | 19 119.6 ms | 4.6 ms | 57.0 ms | 19 181.2 ms |
| Rec.2100 PQ | 42 368.8 ms | 4.6 ms | 144.1 ms | 42 517.5 ms |
| Rec.2100 HLG | 22 033.4 ms | 5.0 ms | 93.2 ms | 22 131.5 ms |

### UHD 3840×2160 Identity

| Encoding | Render/decode | Overlays | Encode | Total |
| --- | ---: | ---: | ---: | ---: |
| Rec.709 Gamma 2.4 | 258.8 ms | 20.5 ms | 265.6 ms | 544.9 ms |
| Rec.2100 PQ | 660.5 ms | 20.6 ms | 645.8 ms | 1 326.9 ms |
| Rec.2100 HLG | 305.2 ms | 21.0 ms | 444.3 ms | 770.4 ms |

## Diagnóstico

El coste dominante no es blanking, texto ni encode. En el downscale Lanczos3,
`sampleManaged` vuelve a decodificar el mismo píxel fuente para cada tap y cada
píxel de salida. PQ amplifica el problema porque ST 2084 ejecuta varias
potencias por muestra.

La primera optimización debe eliminar decode y cálculo de pesos redundantes en
el sampler CPU. No hay todavía evidencia que justifique introducir una ruta GPU
o cambiar la interfaz pública.

## Optimización 1 — decode once

La ruta gestionada se divide ahora en un único contrato host-independent:

1. decode de cada píxel Source una vez a RGBA float display-light linear
   premultiplicado;
2. resampling desde esa superficie ya decodificada;
3. overlays;
4. encode final una vez.

Identity decodifica directamente al working image. Los demás placements usan
un scratch con bounds de Source aportado por el adaptador host. La ruta anterior
que ejecutaba PQ/HLG dentro de cada tap fue eliminada.

### Fuente 4608×3164 → review 1920×1080

| Encoding | Baseline total | Decode once | Ganancia |
| --- | ---: | ---: | ---: |
| Rec.709 Gamma 2.4 | 19 181.2 ms | 7 164.0 ms | 2.68× |
| Rec.2100 PQ | 42 517.5 ms | 7 917.7 ms | 5.37× |
| Rec.2100 HLG | 22 131.5 ms | 7 264.7 ms | 3.05× |

Los checksums arm64 del raster completo permanecen idénticos al baseline. El
probe universal `512×352 → 320×180` conserva también exactamente sus tres
checksums previos tanto en arm64 como en x86_64.

El primer paso utilizaba un scratch RGBA float equivalente al raster Source;
`4608×3164` requería aproximadamente 222 MiB. La optimización 3 elimina esa
superficie completa.

## Optimización 2 — pesos de resampling precomputados

El sampler construye una vez los taps y pesos separables de X e Y para el
`renderWindow`. Cada píxel conserva el mismo bucle Y→X y el mismo orden de suma;
solo desaparecen las evaluaciones repetidas de `sin`, divisiones y coordenadas.

| Encoding | Baseline total | Decode once | + pesos precomputados | Ganancia total |
| --- | ---: | ---: | ---: | ---: |
| Rec.709 Gamma 2.4 | 19 181.2 ms | 7 164.0 ms | 3 082.3 ms | 6.22× |
| Rec.2100 PQ | 42 517.5 ms | 7 917.7 ms | 3 841.0 ms | 11.07× |
| Rec.2100 HLG | 22 131.5 ms | 7 264.7 ms | 3 364.8 ms | 6.58× |

Los checksums completos arm64 siguen siendo idénticos al baseline. No se ha
introducido SIMD, threading ni GPU.

## Optimización 3 — cache acotado de filas decodificadas

El sampler procesa el `renderWindow` de arriba abajo. Mantiene únicamente las
filas Source necesarias para el soporte vertical Lanczos actual y las reutiliza
entre filas de salida adyacentes. Cada fila requerida se decodifica una vez;
Identity continúa decodificando directamente al working image y usa cero bytes
de cache.

| Encoding | Baseline total | Opt. 2 | Cache de filas | Ganancia total |
| --- | ---: | ---: | ---: | ---: |
| Rec.709 Gamma 2.4 | 19 181.2 ms | 3 082.3 ms | 1 109.3 ms | 17.29× |
| Rec.2100 PQ | 42 517.5 ms | 3 841.0 ms | 1 822.2 ms | 23.33× |
| Rec.2100 HLG | 22 131.5 ms | 3 364.8 ms | 1 073.4 ms | 20.62× |

En `4608×3164 → 1920×1080` se decodifican 3164 filas y el cache pico es
2 654 208 bytes, aproximadamente 2.53 MiB: una reducción de alrededor del
98.9 % respecto al scratch completo. Los tres checksums permanecen idénticos.

UHD Identity conserva sus checksums, no usa cache de filas y mejora levemente:
497 ms Rec.709, 1273 ms PQ y 708 ms HLG.

## Optimización 4 — bandas mediante la suite OFX del host

El plugin no crea un pool privado. Consulta `OfxMultiThreadSuiteV1` y entrega al
host bandas Y disjuntas para el render y el encode. El número de workers queda
limitado a 24: en este host 32 hilos no aportan una mejora consistente y elevan
el cache agregado de filas. Si la suite no está disponible, el mismo contrato
actual se ejecuta en una sola banda y el log deja constancia de la capacidad.

Medición `4608×3164 → 1920×1080`, 24 bandas, incluyendo encode paralelo:

| Encoding | Render | Overlays | Encode | Total | Ganancia vs. baseline |
| --- | ---: | ---: | ---: | ---: | ---: |
| Rec.709 Gamma 2.4 | 57.7 ms | 4.5 ms | 4.2 ms | 66.4 ms | 289× |
| Rec.2100 PQ | 97.5 ms | 4.5 ms | 11.0 ms | 113.0 ms | 376× |
| Rec.2100 HLG | 54.6 ms | 4.4 ms | 7.0 ms | 66.0 ms | 335× |

El cache pico agregado de las 24 bandas es 63 700 992 bytes, unos 60.75 MiB.
Los checksums completos coinciden exactamente con una banda para las tres
curvas. El benchmark usa `std::thread` únicamente para aislar esta evaluación;
la implementación OFX de producción usa exclusivamente la suite del host.

## Decisión de backend

SIMD manual no se incorpora: exigiría implementaciones distintas para arm64 y
x86_64 y una validación numérica separada.

La medición posterior en Resolve sí justificó GPU: el backend Metal completo
mantiene 25 fps con blanking semitransparente en el clip real que rendía
aproximadamente a 6 fps por CPU. Windows usa el backend OpenCL del mismo
contrato visual. El host selecciona GPU o memoria CPU antes de Render; no hay
un selector de backend por nodo.

## Validación en Fusion Standalone

El bundle universal `1.1.0` instalado contiene `arm64` y `x86_64`. El smoke
acumulativo P1–P5 pasó en Fusion Standalone 21.0.4 el 20 de agosto de 2026:

- `OfxMultiThreadSuiteV1` disponible;
- 24 workers reales para render y 24 para encode;
- Source `4608×3164` y Output `1920×1080` ejercitados;
- cache de filas activo en placements con resampling;
- Rec.709, PQ, HLG y Auto unknown renderizados;
- sin regresiones en geometría, blanking, seis anclajes libres, outline,
  sombra ni campos calculados.

El chequeo visual final de `P4_REC709_DISPLAY_LINEAR_COMPOSITE`, usando el
renderer P5 instalado, fue aprobado por el usuario. La imagen conserva raster,
crop, blanking, color y texto respecto al checkpoint P4 validado.

## Equivalencia

El benchmark emite un checksum cuantizado útil para comparar ejecuciones dentro
de una misma arquitectura. arm64 y x86_64 no producen un hash global idéntico
por diferencias de `libm`; por ello el hash no se usa como requisito universal.
La aceptación de una optimización se hace con comparación dentro de cada
arquitectura, tests acumulativos y smoke visual de Fusion. Todas las
optimizaciones P5 conservan los checksums cuantizados exactos de cada
arquitectura con una y 24 bandas.
