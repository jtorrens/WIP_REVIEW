# P2a Six Zone Overlay — estado de implementación

**Alcance:** seis zonas estáticas, offsets y overrides; sin outline ni shadow.

## Contrato implementado

- Zonas `TL`, `TC`, `TR`, `BL`, `BC` y `BR`, desactivadas por defecto.
- Enabled y string UTF-8 independientes por zona.
- Alineación horizontal y crecimiento vertical derivados de la zona.
- Cada zona es un anclaje libre respecto al frame completo; el texto conserva
  íntegramente su tamaño y contenido aunque se solape o salga del raster.
- Padding, familia, estilo, tamaño, color y opacity globales.
- Overrides opcionales por zona de tamaño, color RGBA y opacity.
- Offset X normalizado al ancho y Offset Y normalizado a la altura del Output.
- X positivo mueve a la derecha; Y positivo mueve hacia arriba.
- Composición posterior a placement y blanking, en orden estable
  `TL → TC → TR → BL → BC → BR`.
- Contrato único clean-forward: P2a elimina el parámetro de texto único y no
  mantiene rutas de render, aliases ni migraciones de versiones anteriores.
- Log `TEXT_ZONE` por zona con texto, overrides, fuente resuelta, tamaño,
  máscara, origen, offset, color y opacity efectivos.

## Fuera de alcance

- outline y drop shadow;
- campos calculados;
- transformaciones OCIO de píxel, GPU y presets.

## Tests host-independent

- offsets X/Y convertidos respecto a los ejes correctos del Output;
- offsets positivos y negativos sobre anclas opuestas;
- los seis anclajes, padding, orientación de máscara y composición permanecen
  cubiertos.

## Automatización Fusion

El smoke acumulativo añade:

- `AUTOMATED_P2A_SIX_ZONES`;
- `AUTOMATED_P2A_OVERRIDES`;
- `AUTOMATED_P2A_WITH_BLANKING`.

Valida seis máscaras simultáneas, UTF-8, tamaño 60.48 px, color verde, opacity
0.25, offset normalizado con origen calculado y texto sobre blanking.

La composición visual añade:

- `P2A_SIX_ZONES`;
- `P2A_OVERRIDES`;
- `P2A_OFFSETS_INWARD`;
- `P2A_WITH_BLANKING`.

## Resultado automático — aprobado

Ejecutado el 19 de agosto de 2026 con el bundle universal instalado en
Fusion Studio 21.0.4 para macOS:

- 19 de 19 renderizaciones completadas;
- los seis eventos `TEXT_ZONE` presentan máscara y origen propios;
- el override TL resuelve `fontSize = 0.056` a 60.48 px;
- el override TC resuelve RGBA efectivo `[0, 1, 0, 1]`;
- el override BR resuelve opacity efectiva `0.25`;
- el offset BL `[0.05, 0.04]` produce el origen esperado `[125, 65]`;
- el texto se compone después del blanking.

FusionScript expone cada color OFX como cuatro entradas escalares (`Red`,
`Green`, `Blue`, `Alpha`). Los harnesses usan directamente esas entradas del
host; el plugin no contiene adaptadores ni rutas alternativas para scripting.

## Validación visual — aprobada

Validación humana completada el 19 de agosto de 2026 en Fusion Studio 21.0.4:

- `P2A_SIX_ZONES`: seis zonas simultáneas, UTF-8 y alineaciones correctas;
- `P2A_OVERRIDES`: tamaño, verde RGBA, opacity y offset independientes;
- `P2A_OFFSETS_INWARD`: signos, ejes y alineaciones correctos en las seis zonas;
- `P2A_WITH_BLANKING`: seis textos completos sobre las bandas superior e
  inferior.

No se observaron recortes, desplazamientos incorrectos ni interferencias entre
zonas.
