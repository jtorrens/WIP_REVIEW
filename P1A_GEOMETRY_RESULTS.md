# P1a Geometry/Placement — estado de implementación

**Versión:** `WIPReviewProbe.ofx` 0.2.0  
**Alcance:** checkpoint intermedio acordado; no equivale al P1 completo de la
especificación.

## Incluido

- Canvas `Host Raster` y `Requested Review Raster`.
- Placement CPU `Identity`, `Fit`, `Fill / Crop`, `Stretch` y `1:1`.
- Resampling `Bilinear`, `Bicubic (Catmull-Rom)` y `Lanczos3`.
- Soporte de kernel ensanchado al reducir para antialias; Identity usa lectura
  exacta y no ejecuta un filtro.
- Canvas RGBA, negro opaco por defecto.
- Cálculo Fit/Fill con PAR de Source y Output.
- Formatos Byte, Short, Half y Float; componentes RGB y RGBA.
- Escritura limitada al `renderWindow`, bounds con origen no cero y row bytes
  positivos o negativos.
- Filtrado en alpha premultiplicado: una fuente straight se premultiplica antes
  del kernel y se des-premultiplica solo si el Output negociado es straight.
- Log `STATIC_FORMATTER` con placement, filtro, PAR, premultiplicación y canvas.

## Deliberadamente fuera de P1a

- blanking;
- texto y tipografía;
- seis zonas;
- transformaciones OCIO de píxeles;
- GPU;
- presets.

El parámetro P0 `Request Custom Output RoD` se conserva como gate diagnóstico.
`Canvas Mode = Requested Review Raster` solo solicita el RoD configurado si ese
gate está activo. No existe detección por nombre del host ni un workaround
silencioso de Resolve.

## Semántica geométrica

- **Identity:** coordenadas Source y Output alineadas; no escala ni centra. Si
  las dimensiones no coinciden, el exceso se recorta y el resto usa canvas; el
  log emite `identity_raster_mismatch=true implicit_resize=false`.
- **Fit:** escala uniforme máxima que conserva todo el Source; el resto es
  canvas.
- **Fill / Crop:** escala uniforme mínima que cubre el Output; el exceso se
  recorta alrededor del centro.
- **Stretch:** escala X/Y independiente hasta ocupar el Output.
- **1:1:** un píxel físico Source por píxel físico Output, centrado.

Fit/Fill usan `displayWidth = pixelWidth × PAR`. Los bounds físicos entregados
por el host son la base del cálculo, por lo que un render proxy usa de forma
natural el raster correspondiente a su `renderScale`.

## Validación automatizada

Los tests host-independent cubren intersección, canvas, renderWindow, Fit,
Fill/Crop, Stretch, 1:1, Identity, PAR, UInt16, alpha straight/premult y row
bytes negativos. El smoke test de Fusion crea una composición privada con
Source `4608×3164`, ejecuta los cinco placements en Filter-only con Output
`1920×1080`, repite Fit en General y prueba Host Raster `4608×3164`. Todos usan
`AllowResize=1` y Lanczos3. El arnés valida bounds, renderWindow, contexto,
scenario, cada valor de placement, el warning de Identity incompatible y el
evento `STATIC_FORMATTER`.

```sh
cmake --build build --parallel
ctest --test-dir build --output-on-failure
scripts/run_fusion_smoke.sh
```

El smoke de host requiere que el bundle recién construido esté instalado y que
Fusion Standalone 21 pueda abrirse. La composición temporal se cierra bloqueada
para evitar el diálogo de guardado y no modifica la composición activa.

Medición orientativa en Fusion Studio 21.0.4 sobre la máquina de desarrollo:
Fit/Lanczos3 antialiased `4608×3164 → 1920×1080` empleó aproximadamente 7,1 s
en CPU. La matriz completa de siete renders empleó 29,2 s. Es un renderer CPU
de referencia; esta cifra no se presenta como objetivo de rendimiento final.

## Validación visual — Fusion Studio 21.0.4

Validación humana completada el 19 de agosto de 2026 con la carta automatizada
`4608×3164`:

- Fit: carta completa, pillarbox negro a izquierda y derecha.
- Fill/Crop: canvas completo, sin bandas, crop vertical centrado.
- Stretch: canvas completo con deformación no uniforme esperada.
- 1:1: crop físico `1920×1080` centrado, sin escala.
- Identity: crop físico `1920×1080` por coordenadas, alineado abajo a la
  izquierda y claramente distinto de 1:1.
- Host Raster + Identity: Viewer `4608×3164`, carta completa.

Resultado: la semántica visual de los cinco placements y los dos modos de
canvas coincide con el contrato P1a documentado.

## Resultado arquitectónico heredado de P0

- Fusion General/Filter permite `Full-res → OFX → Review Raster`, con la opción
  visible del host **Use plugin RoD for output size** activa.
- Resolve Edit/Color entrega al Filter el raster ya conformado a timeline; usa
  `Full-res → Crop/Resize → OFX → Review HD`.
- El PAR no cuadrado debe normalizarse upstream en Fusion 21 debido a la
  inconsistencia documentada en [HOST_PROBE_RESULTS.md](HOST_PROBE_RESULTS.md).
