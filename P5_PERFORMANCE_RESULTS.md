# P5 Performance — baseline CPU

**Rama:** `p5-performance`  
**Estado:** primera optimización CPU validada fuera del host.

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
  --case fullres_to_hd --encoding all
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

El tradeoff actual es memoria: un placement con resampling reserva un scratch
RGBA float equivalente al raster Source; `4608×3164` requiere aproximadamente
222 MiB. P5 debe evaluar a continuación pesos precomputados y un cache de filas
decodificadas para reducir tiempo y memoria sin reintroducir decode por tap.

## Equivalencia

El benchmark emite un checksum cuantizado útil para comparar ejecuciones dentro
de una misma arquitectura. arm64 y x86_64 no producen un hash global idéntico
por diferencias de `libm`; por ello el hash no se usa como requisito universal.
La aceptación de una optimización se hace con comparación dentro de cada
arquitectura, tests acumulativos y smoke visual de Fusion. Decode once conserva
los checksums cuantizados exactos de cada arquitectura.
