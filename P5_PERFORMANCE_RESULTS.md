# P5 Performance — baseline CPU

**Rama:** `p5-performance`  
**Estado:** baseline previo a optimización.

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

## Equivalencia

El benchmark emite un checksum cuantizado útil para comparar ejecuciones dentro
de una misma arquitectura. arm64 y x86_64 no producen un hash global idéntico
por diferencias de `libm`; por ello el hash no se usa como requisito universal.
La aceptación de una optimización se hará con comparación float dentro de la
arquitectura, tolerancia explícita, tests acumulativos y smoke visual de Fusion.
