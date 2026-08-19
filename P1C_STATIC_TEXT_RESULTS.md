# P1c Static Text — estado de implementación

**Versión de desarrollo:** `WIPReviewProbe.ofx` 0.4.0  
**Alcance:** un único texto estático; todavía no incluye seis zonas ni tokens.

## Contrato implementado

- Un string estático UTF-8, independiente del blanking.
- Enabled=false por defecto.
- Anclajes Top Left, Top Center, Top Right, Bottom Left, Bottom Center y
  Bottom Right.
- Los anclajes top sitúan el límite visible superior en `paddingTop` y crecen
  hacia abajo; los bottom sitúan el límite visible inferior en `paddingBottom`
  y crecen hacia arriba.
- Padding normalizado por eje: left/right 0.015 y top/bottom 0.020 por defecto.
- Tamaño de fuente normalizado respecto a la altura física del Output, 0.028
  por defecto.
- Familia, estilos Regular/Bold/Italic/Bold Italic, color RGB y opacity.
- Raster UTF-8 mediante CoreText/CoreGraphics en macOS.
- Fallback explícito a la fuente de sistema si la familia solicitada no existe;
  el render continúa y registra familia solicitada/resuelta y `fallback=true`.
- Composición posterior a placement y blanking, con alpha premult correcto para
  Output premult o straight-alpha y escritura limitada al `renderWindow`.

El color se compone directamente en el espacio de píxel negociado. La conversión
display-light/OCIO completa no forma parte de este checkpoint.

## Fuera de alcance

- seis zonas simultáneas;
- tokens o metadatos dinámicos;
- outline, shadow y reglas de overflow;
- tipografía final del renderer de producto;
- GPU, presets y transformaciones OCIO de píxel.

## Tests host-independent

- los seis anclajes y sus paddings;
- crecimiento superior hacia abajo e inferior hacia arriba;
- máscara de glyph sobre Output premult y straight-alpha;
- respeto de `renderWindow`;
- raster del string `SECUENCIA ÁRTICO — VERSIÓN 03`;
- incremento de bounds con tamaño/estilo;
- familia inexistente con fallback verificable;
- UTF-8 inválido sin crash ni imagen parcial.

## Automatización Fusion

El smoke acumulativo P1a/P1b/P1c añade cinco renders sobre Output
`1920×1080`:

- `AUTOMATED_P1C_UTF8_TL`;
- `AUTOMATED_P1C_TOP_LARGE`;
- `AUTOMATED_P1C_BOTTOM_LARGE`;
- `AUTOMATED_P1C_FONT_FALLBACK`;
- `AUTOMATED_P1C_OVER_BLANKING`.

El runner valida en `STATIC_TEXT` el texto UTF-8, la fuente resuelta, fallback,
tamaño en píxeles, máscara no vacía, origen de anclaje y orden lógico respecto
al blanking.

```sh
scripts/run_fusion_p1a_smoke.sh
scripts/open_fusion_p1a_visual.sh
```

Los nombres conservan `p1a` para no romper el harness publicado; desde 0.4.0 su
cobertura es acumulativa P1a/P1b/P1c.

## Resultado automático — pendiente de ejecución final

El código y los tests host-independent pasan en arm64. Esta sección se cerrará
con el bundle universal 0.4.0 instalado y el smoke real de Fusion Studio 21.

## Validación visual — pendiente

La composición visual incorpora nodos `P1C_*` para UTF-8 en anclajes opuestos,
crecimiento top/bottom, fallback y texto sobre blanking. La validación humana se
hará después de completar el smoke automático; no se infiere de los logs.
