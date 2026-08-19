# P1b Editorial Blanking — estado de implementación

**Versión de desarrollo:** `WIPReviewProbe.ofx` 0.3.0  
**Alcance:** blanking editorial únicamente; todavía no incluye texto.

## Contrato implementado

- Un único blanking independiente de placement, padding y futuro texto.
- Enabled=false por defecto.
- Presets 1.78, 1.85, 2.00 y 2.39, más Custom.
- Color RGBA negro y opacity 1.0 por defecto.
- Aperture centrado calculado en display aspect con Output PAR.
- Aspect editorial más ancho: barras arriba/abajo.
- Aspect editorial más estrecho: barras izquierda/derecha.
- No cambia Output RoD, bounds ni raster físico.
- Bordes fraccionales con cobertura de área de píxel.
- Composición premult correcta para Output premult y straight-alpha.
- Escritura limitada al `renderWindow`.

P1b compone los valores de color directamente en el espacio de píxel negociado.
La transformación display-light/OCIO completa sigue fuera de este checkpoint,
tal como permite el P1 de la especificación para aislar geometría.

## Tests host-independent

- B01: 2.00 sobre 16:9 produce letterbox.
- B02: opacity 0.5 conserva visible la información exterior.
- B03: Enabled=false no altera ningún píxel.
- B04: aspect más estrecho produce pillarbox.
- Output PAR no cuadrado.
- Borde de aperture fraccional.
- Straight-alpha y límite de renderWindow.

## Automatización Fusion

El smoke acumulativo P1a/P1b añade cuatro escenarios sobre Output `1920×1080`:

- `AUTOMATED_P1B_B01_2_00`;
- `AUTOMATED_P1B_B02_HALF`;
- `AUTOMATED_P1B_B03_OFF`;
- `AUTOMATED_P1B_B04_PILLAR`.

Valida en el log `EDITORIAL_BLANKING` el aspect, Output PAR, aperture físico y
opacity. La composición visual añade nodos `P1B_*` con los mismos casos.

```sh
scripts/run_fusion_p1a_smoke.sh
scripts/open_fusion_p1a_visual.sh
```

Los nombres de los scripts conservan `p1a` para no romper el harness publicado;
desde 0.3.0 su cobertura es acumulativa P1a/P1b.
