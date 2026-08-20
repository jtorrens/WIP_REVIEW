# P2b Outline — estado de implementación

**Versión de desarrollo:** `WIPReviewProbe.ofx` 0.6.0  
**Alcance:** outline global de las seis zonas; sin shadow, overflow ni campos calculados.

## Contrato implementado

- `outlineEnabled`, booleano global, activado por defecto;
- `outlineWidth`, radio normalizado a la altura del Output, default `0.0010`;
- `outlineColor`, RGBA global, negro opaco por defecto;
- `outlineOpacity`, multiplicador global `0–1`, default `1.0`;
- misma configuración para las seis zonas, sin overrides por zona;
- composición `placement → blanking → outline → fill`;
- log `TEXT_OUTLINE` con estado, width normalizado, radio en píxeles, RGBA y
  opacity efectivos;
- `TEXT_ZONE` registra si la zona generó máscara de outline.

## Generación de máscara

El outline se obtiene por dilatación morfológica circular del alpha real
rasterizado por CoreText. No se generan copias desplazadas del string.

La máscara de fill se expande con padding transparente y comparte exactamente
width, height, orientación bottom-up y origen con la máscara dilatada. Por
tanto, el borde exterior —no el fill interior— queda anclado al padding de la
zona. El width persistente permanece independiente de la resolución y solo se
convierte a radio entero durante el render.

## Tests host-independent

- dilatación circular de un glifo sintético sin píxeles diagonales espurios;
- fill y outline comparten dimensiones y conservan metadata de fuente;
- composición outline-first/fill-second con colores distintos;
- cobertura acumulativa de anclas, offsets, premultiplicación y renderWindow.

## Automatización Fusion

El smoke acumulativo añade:

- `AUTOMATED_P2B_OUTLINE_DEFAULT`;
- `AUTOMATED_P2B_OUTLINE_WIDE_RED_HALF`.

Los escenarios anteriores fijan outline off para conservar el aislamiento de
cada prueba. Los casos P2b validan por separado el default negro de un píxel y
un radio de seis píxeles con rojo al 50 %.

La composición visual añade:

- `P2B_OUTLINE_DEFAULT`;
- `P2B_OUTLINE_WIDE_RED_HALF`;
- `P2B_OUTLINE_SIX_ZONES`.

## Fuera de alcance

- drop shadow;
- `Clip`, `Ellipsis`, `ShrinkToFit`, celdas lógicas y `zoneGap`;
- campos calculados;
- transformaciones OCIO de píxeles, GPU y presets.

## Resultado automático — aprobado

Ejecutado el 19 de agosto de 2026 con el bundle universal 0.6.0 instalado en
Fusion Studio 21.0.4 para macOS:

- 21 de 21 renderizaciones completadas;
- el default resuelve width `0.001` a radio `1 px`, RGBA negro opaco y máscara
  `[316, 26]`;
- el caso ancho resuelve width `0.006` a radio `6 px`, RGBA rojo, opacity
  `0.5` y máscara `[326, 36]`;
- ambos eventos `TEXT_ZONE` registran `outline=true`;
- los 19 escenarios acumulativos anteriores continúan aprobados con outline
  desactivado explícitamente para mantener su aislamiento.

## Validación visual — aprobada

Validación humana completada el 19 de agosto de 2026 en Fusion Studio 21.0.4:

- `P2B_OUTLINE_DEFAULT`: fill blanco y outline negro fino, continuo y centrado;
- `P2B_OUTLINE_WIDE_RED_HALF`: outline rojo ancho al 50 %, sin desplazar el
  fill ni el ancla;
- `P2B_OUTLINE_SIX_ZONES`: seis fills blancos con outline rojo opaco sobre las
  bandas superior e inferior, sin recortes ni desplazamientos.

No se observaron duplicaciones del texto, huecos en la máscara ni bordes
anómalos.
