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

El smoke acumulativo añade cuatro escenarios de blanking sobre Output `1920×1080`:

- `AUTOMATED_BLANKING_B01_2_00`;
- `AUTOMATED_BLANKING_B02_HALF`;
- `AUTOMATED_BLANKING_B03_OFF`;
- `AUTOMATED_BLANKING_B04_PILLAR`.

Valida en el log `EDITORIAL_BLANKING` el aspect, Output PAR, aperture físico y
opacity. La composición visual añade nodos `BLANKING_*` con los mismos casos y usa
Fill/Crop upstream para que el blanking se observe aislado de las bandas que
produciría Fit con una fuente de aspect distinto.

```sh
scripts/run_fusion_smoke.sh
scripts/open_fusion_visual.sh
```

La cobertura del harness es acumulativa para geometría, blanking y texto.

## Resultado automático — Fusion Studio 21.0.4

La matriz acumulativa de once renders pasó con el bundle universal 0.3.0:

- B01: aperture físico `[0,60,1920,1020]`, aspect 2.00, opacity 1.0.
- B02: mismo aperture, opacity 0.5.
- B03: `enabled=false`.
- B04: aperture físico `[241.8,0,1678.2,1080]`, Custom 1.33.
- Output PAR 1.0 y Output bounds `1920×1080` en los cuatro casos.

Tiempo orientativo de la matriz completa: 32,8 s en la máquina de desarrollo.

El runner valida cada caso mediante el registro geométrico
`EDITORIAL_BLANKING`; no depende de parámetros auxiliares de diagnóstico.

## Validación visual — Fusion Studio 21.0.4

Validación humana completada el 19 de agosto de 2026 con Source `4608×3164`,
Output `1920×1080` y Fill/Crop para aislar el blanking:

- B01 2.00 / opacity 1.0: barras negras solo arriba y abajo.
- B02 2.00 / opacity 0.5: mismo aperture y exterior visible bajo el negro.
- B03 Off: imagen completa sin bandas.
- B04 Custom 1.33: barras negras solo a izquierda y derecha.

La primera carta visual usaba Fit y mostraba también su canvas lateral negro.
No era un fallo del blanking, pero mezclaba dos operaciones. El harness se
corrigió a Fill/Crop antes de aceptar el resultado visual. La implementación del
plugin no necesitó cambios.

Resultado: geometría, opacity, estado Off e independencia respecto a placement
coinciden con el contrato P1b.
