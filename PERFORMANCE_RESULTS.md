# Rendimiento CPU — identidad localizada

Validación realizada el 20 de agosto de 2026 con el binario universal del
commit `c095a88` en DaVinci Resolve Studio 21.0.4 y Fusion Studio 21.0.4.

## Cambios

1. Fit, Fill, Stretch y 1:1 usan el fast path de Identity cuando su
   transformación resuelta es exactamente 1:1, con bounds y PAR compatibles.
2. En esa ruta, Source se copia directamente a Output. Solo los píxeles
   cubiertos por blanking, fill, outline o shadow se decodifican a luz lineal,
   se componen y se vuelven a codificar.
3. El log acumula hasta 128 registros y hace flush en checkpoints, errores y
   cierre de instancia, en lugar de sincronizar el archivo después de cada
   registro.

El resize real conserva el pipeline de referencia completo: decode lineal,
resampling, overlays y encode. No se introdujo una ruta de calidad reducida.

## Resolve — medición host

Caso: Source y Output Float RGBA premultiplicado 3840×2160, Host Raster,
Placement Fit, Rec.709 Gamma 2.4, blanking y texto. Resolve concedió un hilo al
render y uno al encode en ambas versiones.

| Implementación | Tiempo por frame UHD |
|---|---:|
| Pipeline completo anterior | 640–680 ms |
| Identidad localizada | 142–153 ms |

La mejora observada es aproximadamente 4,5×. Los renders auxiliares 92×46 y
288×162 tardaron 0,3–1,1 ms. El log confirmó `localized_identity=true` y cero
filas decodificadas por el row cache completo.

## Fusion — medición host

El smoke acumulativo aprobó 36 renders. El escenario Host Raster 1:1 activó la
ruta localizada y tardó 54 ms. Los escenarios que cambian raster o placement
mantuvieron `localized_identity=false`.

## Microbenchmark UHD, un hilo

El benchmark incorpora blanking 2.00 al 50 % y seis máscaras de texto. La ruta
localizada modificó el 11,5 % de los 8.294.400 píxeles UHD.

| Encoding | Pipeline completo | Localizado | Mejora |
|---|---:|---:|---:|
| Rec.709 Gamma 2.4 | 490 ms | 84 ms | 5,8× |
| Rec.2100 PQ | 1239 ms | 170 ms | 7,3× |
| Rec.2100 HLG | 688 ms | 110 ms | 6,3× |

## Equivalencia

La aceptación host-independent compara el pipeline completo con el localizado
en Rec.709, PQ y HLG, con alpha premultiplicado variable, blanking
semitransparente y texto. La tolerancia máxima comprobada es `3e-5` en Float32.
Los píxeles no afectados se copian sin round-trip de transferencia.

## Límite restante

Resolve sigue entregando un solo hilo y el plugin mantiene
`SupportsTiles=0`. Los casos con resize real continúan siendo el principal
coste; tiles, reutilización de buffers y GPU quedan fuera de este checkpoint.
