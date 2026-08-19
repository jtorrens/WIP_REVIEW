# P2b Outline — estado de implementación

**Versión de desarrollo:** `WIPReviewProbe.ofx` 0.6.0  
**Alcance:** outline global de las seis zonas; sin shadow, overflow ni tokens.

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
- tokens dinámicos;
- transformaciones OCIO de píxeles, GPU y presets.

## Resultado automático — pendiente

Se completará tras construir e instalar el bundle universal 0.6.0 y ejecutar
el smoke acumulativo en Fusion Studio 21.

## Validación visual — pendiente

Se comprobarán el default negro, width/color/opacity y las seis zonas sobre
blanking después de aprobar la matriz automática.
