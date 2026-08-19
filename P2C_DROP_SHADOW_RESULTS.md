# P2c Drop Shadow — estado de implementación

**Versión de desarrollo:** `WIPReviewProbe.ofx` 0.7.0  
**Alcance:** drop shadow global de las seis zonas; sin overflow ni tokens.

## Contrato implementado

- `shadowEnabled`, booleano global, desactivado por defecto;
- `shadowOffsetX`, normalizado al ancho del Output, default `0.0015`;
- `shadowOffsetY`, normalizado a la altura del Output, default `0.0020`;
- `shadowSoftness`, sigma gaussiana normalizada a la altura del Output, default
  `0.0020`;
- `shadowColor`, RGBA global, negro opaco por defecto;
- `shadowOpacity`, multiplicador global `0–1`, default `0.60`;
- misma configuración para las seis zonas, sin overrides por zona;
- X positivo desplaza a la derecha y Y positivo visualmente hacia abajo;
- composición `placement → blanking → shadow → outline → fill`;
- log `TEXT_SHADOW` con valores normalizados y efectivos en píxeles;
- `TEXT_ZONE` registra si la zona generó máscara de sombra.

## Generación de máscara

La sombra parte del alpha real rasterizado por CoreText. Primero se desplaza la
máscara y después se aplica un blur gaussiano separable al alpha; el RGB no se
difumina después de componer.

Fill, outline y shadow se expanden a un único canvas transparente y conservan
exactamente width, height, orientación bottom-up y origen. La sombra se compone
detrás del outline y del fill. Un fallo al generar la máscara queda registrado
como `glyph_shadow_generation_failed`; no activa una ruta de render alternativa.

La composición usa por ahora el renderer CPU vigente. La conversión completa a
display-light linear sigue fuera de este checkpoint porque depende de la fase
OCIO de píxeles todavía no implementada.

## Tests host-independent

- desplazamiento horizontal y vertical sobre un alpha sintético de `1×1`;
- convención pública Y positiva hacia abajo sobre el raster bottom-up;
- blur suave con centro y vecinos no nulos y conservación aproximada de alpha;
- canvas compartido cuando outline y shadow están activos a la vez;
- cobertura acumulativa de anclas, offsets, premultiplicación y `renderWindow`.

## Automatización Fusion

El smoke acumulativo añade:

- `AUTOMATED_P2C_SHADOW_DEFAULT`;
- `AUTOMATED_P2C_SHADOW_HARD_BLUE`.

Los escenarios anteriores fijan shadow off para conservar el aislamiento de
cada prueba. Los casos P2c validan separadamente el default negro suave y una
sombra azul dura con offset negativo.

La composición visual añade:

- `P2C_SHADOW_DEFAULT`;
- `P2C_SHADOW_HARD_BLUE`;
- `P2C_SHADOW_SIX_ZONES`.

## Fuera de alcance

- `Clip`, `Ellipsis`, `ShrinkToFit`, celdas lógicas y `zoneGap`;
- tokens dinámicos;
- transformaciones OCIO de píxeles, GPU y presets.

## Resultado automático — aprobado

Ejecutado el 19 de agosto de 2026 con el bundle universal 0.7.0 instalado en
Fusion Studio 21.0.4 para macOS:

- 23 de 23 renderizaciones completadas;
- el default resuelve offset `0.0015, 0.0020` a `3, 2 px`, softness `0.0020`
  a sigma `2.16 px`, RGBA negro y opacity `0.60`;
- la máscara compartida del caso default mide `[337, 38]` y registra
  `shadow=true`;
- el caso duro resuelve offset `-0.01, -0.01` a `-19, -11 px`, sigma `0`,
  RGBA azul y opacity `1.0`;
- la máscara compartida del caso duro mide `[377, 35]` y registra
  `shadow=true`;
- no se registraron fallos de rasterización, outline ni shadow;
- los 21 escenarios acumulativos anteriores continúan aprobados con shadow
  desactivado explícitamente para mantener su aislamiento.

## Validación visual

Pendiente de validación humana en Fusion Studio 21.0.4.
