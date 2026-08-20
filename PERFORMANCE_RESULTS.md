# Rendimiento CPU — identidad localizada

Validación realizada el 20 de agosto de 2026 con el binario universal del
commit `39345d4` en DaVinci Resolve Studio 21.0.4 y Fusion Studio 21.0.4.

## Cambios

1. Fit, Fill, Stretch y 1:1 usan el fast path de Identity cuando su
   transformación resuelta es exactamente 1:1, con bounds y PAR compatibles.
2. En esa ruta, Source se copia directamente a Output. El blanking usa una
   pasada fusionada `decode → over → encode` directamente sobre Output. Solo
   las intersecciones con fill, outline o shadow permanecen en el workspace
   lineal para compartir un único encode final con el texto.
3. El log acumula hasta 128 registros y hace flush en checkpoints, errores y
   cierre de instancia, en lugar de sincronizar el archivo después de cada
   registro.
4. Un mapa por filas limita el encode final a los spans tipográficos; el
   blanking semitransparente ya no incorpora sus bandas completas a ese mapa ni
   al workspace Float32.
5. La pasada de blanking se divide en bandas mediante el multithreading OFX.
   El blanking completamente opaco se codifica una vez; solo sus bordes
   fraccionales y las bandas semitransparentes pasan por composición lineal.
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

## Microbenchmark UHD

El benchmark incorpora blanking 2.00 y seis máscaras de texto. Tanto al 50 %
como al 100 %, solo el 3,0 % de los 8.294.400 píxeles UHD permanece en el
workspace lineal; las bandas restantes se resuelven directamente. La tabla
separa la referencia serial de la ejecución con 24 bandas, equivalente a la
ruta solicitada al suite multithread del host.

| Encoding | Pipeline completo, 1 hilo | Blanking 50 %, 1 hilo | Blanking 50 %, 24 bandas |
|---|---:|---:|---:|
| Rec.709 Gamma 2.4 | 481 ms | 71 ms | 41 ms |
| Rec.2100 PQ | 1237 ms | 156 ms | 89 ms |
| Rec.2100 HLG | 687 ms | 97 ms | 59 ms |

## Equivalencia

La aceptación host-independent compara el pipeline completo con el localizado
en Rec.709, PQ y HLG, con alpha premultiplicado variable, color de blanking no
negro, opacidades 50/100 % y texto. La tolerancia máxima comprobada es `3e-5`
en Float32. Los píxeles no afectados se copian sin round-trip de transferencia.

## Backend actual

Metal cubre ahora tanto identidad como resize real, blanking y texto. En la
composición real de Resolve usada durante el desarrollo mantiene 25 fps con
blanking semitransparente; la ruta CPU anterior reproducía ese caso a unos
6 fps. CPU permanece como backend cuando el host entrega memoria convencional.
El plugin mantiene `SupportsTiles=0`.
