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

## Pendiente

- parámetros P4 y resolución Auto/Manual;
- negociación OFX de Source/Output;
- log y warning visible para espacio desconocido;
- smoke automático y validación visual en Fusion Studio 21.0.4.
