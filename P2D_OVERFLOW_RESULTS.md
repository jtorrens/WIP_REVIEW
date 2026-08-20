# P2d Overflow — estado de implementación

**Versión de desarrollo:** `WIPReviewProbe.ofx` 0.8.0  
**Alcance:** celdas lógicas y overflow global de las seis zonas; sin campos calculados.

## Contrato implementado

- `zoneGap`, ancho normalizado completo entre celdas, default `0.010`;
- `overflowMode`: `Clip`, `Ellipsis` o `ShrinkToFit`, default `ShrinkToFit`;
- `minimumFontScale`, límite inferior de reducción, default `0.60`;
- tres celdas horizontales independientes en las filas superior e inferior;
- padding izquierdo y derecho aplicado únicamente a los extremos externos;
- gap simétrico alrededor de los límites `1/3` y `2/3` del Output;
- alineación izquierda, centrada o derecha deducida de la zona dentro de su
  propia celda;
- clipping final de fill, outline y shadow contra la misma celda;
- offsets mueven el raster, pero no desplazan el límite de clipping;
- log global `TEXT_OVERFLOW` y estado efectivo por `TEXT_ZONE`.

En `1920×1080`, con padding `0.015` y gap `0.010`, las celdas son:

```text
Left   [29, 630)
Center [650, 1270)
Right  [1290, 1891)
```

## Políticas

### Clip

Conserva el string y tamaño solicitados. El canvas tipográfico se ancla en su
celda y se recorta exactamente en sus límites.

### Ellipsis

Conserva el tamaño solicitado y sustituye el tail por `…`. Selecciona el
prefijo UTF-8 más largo cuyo raster estilizado cabe. Si el propio ellipsis no
cabe, se conserva y se recorta dentro de la celda.

### ShrinkToFit

Reduce únicamente las zonas que desbordan, nunca supera el tamaño solicitado y
busca el mayor tamaño que cabe. Outline y shadow globales cuentan en la medida.
Si el texto no cabe a `minimumFontScale`, mantiene exactamente ese mínimo y
aplica Clip. Esta es la política interna determinista permitida por la
especificación.

## Tests host-independent

- coordenadas exactas de las tres celdas HD;
- clipping real de composición contra la celda;
- Clip conserva string y escala;
- Ellipsis conserva UTF-8 y genera un raster dentro del ancho disponible;
- ShrinkToFit elige una escala entre mínimo y solicitado;
- texto que no cabe al mínimo registra Clip a escala `0.60`;
- el cálculo de overflow incluye expansión de outline y shadow;
- cobertura acumulativa de anclas, offsets, premultiplicación y `renderWindow`.

## Automatización Fusion

El smoke acumulativo añade:

- `AUTOMATED_P2D_OVERFLOW_CLIP`;
- `AUTOMATED_P2D_OVERFLOW_ELLIPSIS`;
- `AUTOMATED_P2D_OVERFLOW_SHRINK`;
- `AUTOMATED_P2D_OVERFLOW_MIN_CLIP`.

La composición visual añade:

- `P2D_OVERFLOW_CLIP`;
- `P2D_OVERFLOW_ELLIPSIS`;
- `P2D_OVERFLOW_SHRINK`;
- `P2D_OVERFLOW_MIN_CLIP`.

## Fuera de alcance

- campos calculados Frame Relative, Frame y Timecode;
- transformaciones OCIO de píxeles, GPU y presets.

## Resultado automático — aprobado

Ejecutado el 19 de agosto de 2026 con el bundle universal 0.8.0 instalado en
Fusion Studio 21.0.4 para macOS:

- 27 de 27 renderizaciones completadas;
- las celdas registradas son `[29,630]`, `[650,1270]` y `[1290,1891]`;
- Clip conserva string y escala `1.0`; su raster de `1931 px` queda limitado a
  la celda izquierda de `601 px`;
- Ellipsis produce `ELLIPSIS PRESERVE…`, escala `1.0` y raster de `597 px`
  dentro de la celda central de `620 px`;
- ShrinkToFit resuelve el caso derecho a escala `0.820905`, tamaño efectivo
  `53.194614 px` y ancho exacto `601 px`;
- el caso que no cabe al mínimo conserva escala `0.600000`, tamaño
  `38.880000 px` y registra `clipped=true`;
- no se registraron fallos de rasterización, outline ni shadow;
- los 23 escenarios acumulativos anteriores continúan aprobados.

## Validación visual — aprobada

Validación humana completada el 19 de agosto de 2026 en Fusion Studio 21.0.4:

- `P2D_OVERFLOW_CLIP`: tres textos grandes quedan recortados dentro de sus
  celdas roja, verde y azul, con gaps limpios y sin invasión;
- `P2D_OVERFLOW_ELLIPSIS`: las seis zonas permanecen separadas y los textos
  largos terminan en `…`;
- `P2D_OVERFLOW_SHRINK`: la fila superior reduce cada zona de forma
  independiente y los tres textos cortos inferiores conservan mayor tamaño;
- `P2D_OVERFLOW_MIN_CLIP`: el texto al mínimo queda centrado y recortado
  simétricamente dentro de la celda central sobre blanking.

No se observaron cruces entre columnas, pérdida de gaps ni cambios de tamaño en
zonas que ya cabían.
