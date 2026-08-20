# P3 Calculated Fields — estado de implementación

## Contrato

Cada zona compone dos entradas, en este orden:

1. **Text**: prefijo UTF-8 completamente literal.
2. **Calculated Field**: `None`, `Frame Relative`, `Frame`, `Timecode` o
   `Date`.

Resolve añade al final `Source Frame` y `Source Filename`, publicados mediante
las extensiones oficiales `OfxImageEffectPropSrcFrame` y
`OfxImageEffectPropSrcFilePath`. Fusion Standalone no muestra estas opciones
porque su adaptador OFX no expone el grafo ni la metadata arbitraria del nodo
anterior.

No se interpretan expresiones entre llaves dentro de **Text**. Por ejemplo,
`Text = "FR: "` con `Calculated Field = Frame` produce `FR: 1001`, mientras
`Text = "{frame}"` con `None` conserva literalmente `{frame}`.

## Parámetros globales

- `Frame Relative Base`: primer número de Frame Relative; default `1`.
- `Frame Start`: frame absoluto en el primer frame del efecto; default `1001`.
- `Review Date`: fecha estable `YYYY-MM-DD`, inicializada al crear el efecto y
  editable por el usuario.
- `FPS Mode`: `AutoFromHost` u `Override`.
- `FPS Override`: default `24.0`.
- `Timecode Start`: timecode global del primer frame del efecto.
- `Drop Frame Mode`: `Auto`, `NonDrop` o `Drop`.

Frame Relative, Frame y Timecode se calculan desde `round(effectTime)`. Auto
aplica drop-frame solo a 29.97 y 59.94. Un FPS inválido, un Timecode Start
inválido o una petición Drop incompatible se registra explícitamente.

## Variación temporal

El output se declara frame-varying únicamente cuando una zona selecciona
Frame Relative, Frame, Timecode o, en Resolve, Source Frame. Date y Source
Filename son estables.

## Cobertura automática

- composición de prefijo + cada campo calculado;
- prefijo con llaves conservado literalmente;
- incremento temporal y redondeo half-away-from-zero;
- timecode NonDrop a 24, 25, 30 y 23.976;
- 29.97 DF: `00:00:59;29 → 00:01:00;02`;
- fecha, source frame y source filename;
- configuración inválida con warning controlado;
- smoke acumulativo en Fusion para campos portables.

Los campos exclusivos de Resolve aún requieren el chequeo empírico final en
Resolve; si el host no publica una propiedad en una llamada concreta, el OFX
deja el valor vacío y lo registra, sin fabricar datos.
