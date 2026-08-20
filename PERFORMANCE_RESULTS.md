# Rendimiento CPU — identidad localizada

Validación realizada el 20 de agosto de 2026 con el binario universal del
commit `39345d4` en DaVinci Resolve Studio 21.0.4 y Fusion Studio 21.0.4.

## Cambios

1. Fit, Fill, Stretch y 1:1 usan el fast path de Identity cuando su
   transformación resuelta es exactamente 1:1, con bounds y PAR compatibles.
2. En esa ruta, Source se copia directamente a Output. Solo los píxeles
   cubiertos por blanking, fill, outline o shadow se decodifican a luz lineal,
   se componen y se vuelven a codificar.
3. El log acumula hasta 128 registros y hace flush en checkpoints, errores y
   cierre de instancia, en lugar de sincronizar el archivo después de cada
   registro.
4. Un mapa por filas limita el encode final a los spans que contienen píxeles
   modificados; el workspace Float32 no se inicializa fuera de esos píxeles.
5. El blanking completamente opaco se codifica una vez y se escribe
   directamente. Solo sus bordes fraccionales pasan por composición lineal.
6. Cada instancia conserva las seis máscaras tipográficas resueltas mientras
   texto, fuente, tamaño, overflow, outline y shadow no cambien.

El resize real conserva el pipeline de referencia completo: decode lineal,
resampling, overlays y encode. No se introdujo una ruta de calidad reducida.

## Resolve — medición host

Caso: Source y Output Float RGBA premultiplicado 3840×2160, Host Raster,
Placement Fit, Rec.709 Gamma 2.4, blanking y texto. Resolve concedió un hilo al
render y uno al encode en ambas versiones.

| Implementación | Tiempo por frame UHD |
|---|---:|
| Pipeline completo anterior | 640–680 ms |
| Identidad localizada inicial | 142–153 ms |
| Localizada, blanking opaco y caché de texto | 14,8–15,5 ms |

El clip real del proyecto `FOQN_2` reprodujo 25 frames en cada segundo completo
de una prueba de seis segundos: tiempo real sostenido a 25 fps. La mejora del
OFX es aproximadamente 10× respecto a la primera ruta localizada y 43× frente
al pipeline completo. El log confirmó `localized_identity=true`,
`text_cache_hits=1`, 14.485 píxeles lineales localizados y cero filas
decodificadas por el row cache completo.

## Fusion — medición host

El smoke acumulativo aprobó 36 renders. El escenario Host Raster 1:1 activó la
ruta localizada y bajó de 54 a 27,7 ms. Los escenarios que cambian raster o
placement mantuvieron `localized_identity=false`.

## Microbenchmark UHD, un hilo

El benchmark incorpora blanking 2.00 y seis máscaras de texto. Al 50 % la ruta
localizada modifica el 11,5 % de los 8.294.400 píxeles UHD. Al 100 %, las
bandas se escriben directamente y solo el 3,0 % permanece en el workspace
lineal.

| Encoding | Pipeline completo | Blanking 50 % | Blanking 100 % |
|---|---:|---:|---:|
| Rec.709 Gamma 2.4 | 492 ms | 69 ms | 21 ms |
| Rec.2100 PQ | 1245 ms | 158 ms | 40 ms |
| Rec.2100 HLG | 697 ms | 98 ms | 28 ms |

## Equivalencia

La aceptación host-independent compara el pipeline completo con el localizado
en Rec.709, PQ y HLG, con alpha premultiplicado variable, color de blanking no
negro, opacidades 50/100 % y texto. La tolerancia máxima comprobada es `3e-5`
en Float32. Los píxeles no afectados se copian sin round-trip de transferencia.

## Límite restante

Resolve sigue entregando un solo hilo y el plugin mantiene
`SupportsTiles=0`. Los casos con resize real continúan siendo el principal
coste; tiles, reutilización de buffers y GPU quedan fuera de este checkpoint.
