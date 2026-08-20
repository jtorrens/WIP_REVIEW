# Inspector UX — contrato vigente

El inspector se organiza por tarea y no por fases de desarrollo. OpenFX recibe
cinco páginas y una jerarquía equivalente para hosts que ignoren el layout
paginado.

## Páginas

| Orden | Página | Controles |
|---:|---|---|
| 1 | Canvas | Capacidad de host, Canvas Mode, raster preset/Custom, placement, filtro, canvas y blanking |
| 2 | Typography | Fuente, estilo, tamaño, color, opacity, padding, gap, overflow, outline y shadow |
| 3 | Zones | TL, TC, TR, BL, BC y BR con texto, offsets y overrides |
| 4 | Timing | Frame Relative Base, Frame Start, FPS y timecode |
| 5 | Color | Modo de interpretación, espacio fallback/manual, Graphics White y peak HLG |

## Jerarquía

Los grupos raíz aparecen en este orden:

1. Canvas — abierto;
2. Editorial Blanking — abierto;
3. Typography — abierto, con Outline y Drop Shadow cerrados;
4. Zones — cerrado, con sus seis zonas cerradas;
5. Timing — cerrado;
6. Managed Color — cerrado.

Dentro de una zona se evita repetir su abreviatura: las etiquetas son
`Enabled`, `Text`, `Offset X/Y` y sus overrides.

## Estados contextuales

| Condición | Controles activos |
|---|---|
| Host validado + Requested Review Raster | Review Raster |
| Review Raster = Custom | Custom Width/Height |
| Blanking Enabled | Aspect, Color y Opacity |
| Blanking Aspect = Custom | Custom Aspect |
| Outline Enabled | Width, Color y Opacity |
| Shadow Enabled | Offset X/Y, Softness, Color y Opacity |
| Override de zona activo | Valor correspondiente |
| FPS Mode = Override | FPS Override |
| Graphics White Mode = Manual | Graphics White Nits |
| Auto color o espacio manual HLG | HLG Peak Nits |

Los textos y offsets de zona siguen editables aunque la zona esté apagada, para
permitir prepararla antes de activarla. `Fallback / Manual Space` permanece
editable en Auto porque define la interpretación explícita cuando el host no
publica colourspace.

## Raster presets

```text
HD      1920 × 1080
UHD     3840 × 2160
DCI 2K  2048 × 1080
DCI 4K  4096 × 2160
Custom  1…32768 × 1…32768
```

Los presets y Custom convergen en un único `RasterSize` host-independent antes
de responder al RoD. No existe una ruta de render por preset.

## Compatibilidad de presentación

Las páginas usan `kOfxParamTypePage`, `kOfxParamPropPageChild` y
`kOfxPluginPropParamPageOrder`. Si un host no admite páginas, los grupos siguen
presentando el mismo orden y contenido. Esto es una adaptación de presentación
del contrato actual.

## Validación en hosts

Validado el 20 de agosto de 2026 con el bundle universal del commit `f42c34c`:

- Fusion Studio 21.0.4 informó `max_pages=-1`, aceptó la definición de cinco
  páginas y presentó la jerarquía de grupos. Los estados contextuales se
  reflejaron correctamente en el inspector.
- DaVinci Resolve Studio 21.0.4 informó `max_pages=0`, cargó el descriptor sin
  errores y presentó la misma jerarquía. Canvas Mode, Review Raster y Custom
  Width/Height quedaron deshabilitados en Host Raster, según el contrato.
