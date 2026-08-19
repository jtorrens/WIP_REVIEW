# P3 Dynamic Tokens — estado de implementación

**Versión de desarrollo:** `WIPReviewProbe.ofx` 0.9.0  
**Alcance:** `{frame_rel}`, `{frame}`, `{timecode}` y cache frame-varying.

## Contrato implementado

- `{frame_rel} = round(effectTime) + frameRelativeBase`, default base `1`;
- `{frame} = round(effectTime) + frameStart`, default start `1001`;
- `{timecode}` usa effect time en frames, FPS, start y modo drop-frame;
- `fpsMode`: `AutoFromHost` o `Override`;
- `fpsOverride`, default neutral `24.0`;
- `timecodeStart`, default neutral `00:00:00:00`;
- `dropFrameMode`: `Auto`, `NonDrop` o `Drop`;
- Auto aplica drop-frame solo a 29.97 y 59.94;
- NonDrop usa `HH:MM:SS:FF`; Drop usa `HH:MM:SS;FF`;
- los tokens desconocidos permanecen literales;
- las seis strings son esclavas de Clip Preferences;
- `OfxImageEffectFrameVarying=true` solo si una string contiene un token V1.

Los parámetros no infieren semántica de producción. `frameStart` debe recibir el
valor correcto del usuario o de una integración externa.

## Timecode inválido

Un FPS inválido, start inválido o petición Drop incompatible se registra. El
cálculo usa `{frame_rel}` como offset interno y conserva intacta la string del
usuario; no existe una aproximación silenciosa. Para poder expresar el fallback
como timecode cuando el FPS también es inválido, el cálculo interno usa nominal
24 y registra `fps_valid=false`.

## Tests host-independent

- expansión combinada de los tres tokens;
- incremento exacto de uno por frame y round half-away-from-zero;
- token desconocido conservado literalmente;
- timecode NonDrop a 24, 25, 30 y 23.976;
- 29.97 DF: `00:00:59;29 → 00:01:00;02`;
- 29.97 DF exacto a diez minutos: `00:10:00;00`;
- start `01:00:00;00` preservado;
- start inválido y Drop incompatible registrados con cálculo controlado.

## Automatización Fusion

El smoke acumulativo añade:

- `AUTOMATED_P3_TOKENS`, renderizado en frames 0 y 1;
- `AUTOMATED_P3_TIMECODE_DF`, renderizado en 1799 y 1800;
- `AUTOMATED_P3_INVALID_TIMECODE`.

También valida Clip Preferences con output estático y frame-varying.

La composición visual añade:

- `P3_TOKENS_24_NDF`;
- `P3_TIMECODE_DF`;
- `P3_INVALID_TIMECODE`.

## Fuera de alcance

- tokens semánticos de producción;
- transformaciones OCIO de píxeles, GPU y presets.

## Resultado automático

**Aprobado el 19 de agosto de 2026** con el bundle universal 0.9.0 instalado en
Fusion Studio 21.0.4 para macOS.

- smoke acumulativo: **32/32 renders aprobados**;
- las 27 pruebas acumuladas de P1/P2 continúan aprobadas;
- las instancias sin tokens declaran `output_frame_varying=false`;
- las instancias con tokens V1 declaran `output_frame_varying=true`;
- frame 0: `REL 1 ABS 1001 TC 00:00:00:00 UNKNOWN {shot}`;
- frame 1: `REL 2 ABS 1002 TC 00:00:00:01 UNKNOWN {shot}`;
- 29.97 DF, frame 1799: `DF 00:00:59;29`;
- 29.97 DF, frame 1800: `DF 00:01:00;02`;
- start inválido con Drop incompatible: salida controlada
  `INVALID TC 00:00:00:01`, con `used_timecode_fallback=true` y
  `RENDER_WARNING timecode_resolution_fallback=true` en el log.

El token desconocido `{shot}` se conserva literalmente, sin sustitución ni
ruta de compatibilidad.

## Validación visual

Pendiente de validación humana en Fusion Studio 21.0.4.
