# P4 Managed Color — estado de implementación

**Rama de desarrollo:** `p4-managed-color`  
**Alcance:** color display-referred gestionado, Graphics White y composición
display-light linear.

## Contrato aprobado

La transformación fotográfica completa se delega al `Color Space Transform`
nativo del host:

```text
Source
→ Host CST: espacio de cámara/escena → espacio final de review
→ WIPReview OFX: decode → gráficos display-light linear → encode
→ Output
```

WIPReview recibe y devuelve el mismo espacio display-referred. No contiene un
segundo renderer fotográfico, tone mapping creativo ni gamut mapping paralelo.
La negociación OFX 1.5.1 se conserva para detectar y declarar el espacio real.
Si el host no publica una interpretación válida, la selección manual explícita
es obligatoria y se registra un warning; nunca se asume `linear sRGB`.

## Backend inicial

Un único backend CPU implementa:

- Rec.709 Gamma 2.4;
- Rec.2100 PQ / ST 2084;
- Rec.2100 HLG con OOTF dependiente del peak del display;
- representación intermedia float en luz de display;
- unidad lineal `1.0 = Graphics White`;
- Graphics White automático: 100 nits SDR, 203 nits PQ y 20.3 % del peak HLG;
- Graphics White manual para workflows que lo requieran.

Los colores del picker son gráficos de display y se expresan directamente con
relación a Graphics White. No atraviesan el CST fotográfico del host.

## Tests host-independent

- Rec.709: decode de `0.5` y encode de una mezcla lineal al 50 %;
- PQ: Graphics White 203 nits codifica a `0.5806889`;
- PQ: peak de 10 000 nits codifica a `1.0`;
- HLG: señal neutral `0.75` representa aproximadamente 203 nits en 1000 nits;
- round-trip Rec.709, PQ y HLG, incluido RGB no neutral;
- defaults automáticos de Graphics White.

Estado del checkpoint matemático: **3/3 suites aprobadas con warnings como
errores**.

El core dispone ya del recorrido de imagen gestionado:

1. convierte Source premultiplicado a RGB straight;
2. decodifica el RGB display-referred;
3. vuelve a premultiplicar para resampling y composición lineal;
4. mantiene un working image RGBA float;
5. des-premultiplica, codifica una sola vez y aplica la convención alpha del
   Output.

Las pruebas confirman tanto Output straight como premultiplicado y descartan
explícitamente la mezcla directa en valores Gamma 2.4.

## Parámetros P4

- `Color Space Mode`: Auto from Host / Manual Override;
- `Manual Color Space`: Rec.709 Gamma 2.4 / Rec.2100 PQ / Rec.2100 HLG;
- `Graphics White Mode`: Auto / Manual;
- `Graphics White Nits`;
- `HLG Peak Nits`.

Input y Output comparten una única interpretación display-referred. En Auto,
un espacio de host desconocido, `Raw` o scene-linear activa la interpretación
manual seleccionada y publica un warning persistente en el nodo y en el log.
El estado `MANAGED_COLOR` registra espacio del host, reconocimiento, selección
efectiva, Graphics White, peak HLG, working premultiplicado y
`encode_count=1`.

## Negociación OFX 1.5.1

- Auto no publica una preferencia de Source que pueda contradecir el CST
  upstream;
- Manual solicita al host el espacio display seleccionado mediante
  `OfxImageClipPropPreferredColourspaces_Source`;
- el render comprueba siempre `kOfxImageClipPropColourspace` en la imagen y,
  si no está disponible, en el clip;
- Output se declara `OfxColourspace_Source`, porque WIPReview devuelve el mismo
  espacio que interpreta;
- `colorSpaceMode` y `manualColorSpace` invalidan Clip Preferences.

Fusion 21 General no publicó colourspace durante P0. En ese contexto Auto debe
activar el warning y Manual es el contrato operativo después del CST nativo.
Fusion Studio 21.0.4 tampoco invoca `GetOutputColourspace`; por ello el smoke
valida la preferencia de Source y el espacio efectivo del render, pero no exige
una callback que el host no realiza.

## Resultado automático

**Aprobado el 19 de agosto de 2026** con el bundle universal 1.0.0 instalado en
Fusion Studio 21.0.4 para macOS.

- matriz local warnings/Release/universal: **3/3 suites** en cada build;
- smoke acumulativo de Fusion: **36/36 renders aprobados**;
- los 32 renders acumulados P1–P3 continúan aprobados;
- Rec.709 Manual: Graphics White automático `100` nits, working
  display-light linear premultiplicado y `encode_count=1`;
- PQ Manual: preferencia Source `rec2100_pq_display` aceptada por
  Clip Preferences, Graphics White automático `203` nits y `encode_count=1`;
- HLG Manual: preferencia Source `rec2100_hlg_display`, peak `1000` nits,
  Graphics White derivado `203` nits y `encode_count=1`;
- Auto en Fusion: host colourspace `<ErrUnknown>`, sin preferencia Source,
  interpretación manual determinista Rec.709 y `RENDER_WARNING` explícito;
- Fusion no invocó `GetOutputColourspace`, coherente con P0.

La segunda ejecución completa tardó aproximadamente 5 min 36 s. Se conserva
como baseline del CPU reference renderer para P5; P4 no introduce atajos que
alteren el resultado cromático.

## Validación visual

**Aprobada el 20 de agosto de 2026** en Fusion Studio 21.0.4.

- Rec.709: salida directa al viewer, raster completo, blanking uniforme al
  50 %, texto estable y ausencia de halos o artefactos;
- PQ: la salida raw codificada se ve gris en un viewer SDR, como corresponde a
  `203` nits codificados a `0.5806889`; después del Color Space Transform
  nativo `Rec.2100 ST2084 → Rec.709`, con `HDR 203 Nits Diffuse White`, el
  blanco coincide con Rec.709;
- HLG: después del Color Space Transform nativo
  `Rec.2100 HLG EOTF → Rec.709`, el blanco de `203` nits coincide con Rec.709;
- PQ y HLG se compararon contra Rec.709 mediante wipe A/B dentro del mismo
  viewer. Las conexiones y las EOTF efectivas se verificaron además mediante
  la API de Fusion;
- Auto/unknown: salida Rec.709 visualmente correcta y estable; el smoke registra
  `<ErrUnknown>`, el override manual determinista y `RENDER_WARNING`.

El caso Rec.709 del harness se denomina
`P4_REC709_DISPLAY_LINEAR_COMPOSITE`: “display-linear” describe el espacio de
composición interno. La salida del nodo está codificada en Rec.709 Gamma 2.4 y
se visualiza directamente, sin CST downstream.
