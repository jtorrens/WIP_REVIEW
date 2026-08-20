# ShotConfig v0.1

`ShotConfig` es el registro persistente de configuración de un plano para
Fusion. No procesa imágenes, no depende del OFX de `wip_review` y no conecta
controles a otros nodos mediante expresiones. Su única operación en v0.1 es
resolver templates y escribir rutas explícitamente en los `Clip` de los
Loaders y Savers registrados.

La separación con `wip_review` es permanente a nivel de código, ramas y
build. Si un módulo futuro genera WIPs, utilizará el OFX ya instalado como un
nodo de Fusion en runtime. No importará su código fuente ni creará una
dependencia entre ambos proyectos.

La implementación está validada con Fusion Standalone 21.0.4.

## Archivos

- `build_shot_config.lua`: crea o reconstruye el `GroupOperator`.
- `apply_shot_config.lua`: resuelve, valida y aplica los targets.
- `color_enum_catalog.lua`: descubre y reconcilia los enums del CST nativo.
- `color_enum_seed.lua`: orden y etiquetas curadas iniciales.
- `color_enum_labels.json`: catálogo editable generado para el host actual.
- `sync_color_enums.sh`: abre Fusion si es necesario y actualiza el catálogo.
- `create_example_comp.sh`: genera y abre una composición de ejemplo persistente.
- `examples/ShotConfig_Example.comp`: composición base editable.
- `tests/fusion_host_tests.lua`: casos de aceptación en una comp temporal.
- `tests/run_fusion_tests.sh`: arranca o reutiliza Fusion 21 y ejecuta los
  casos de aceptación; al terminar deja abierta una comp para pruebas manuales.

No es necesario instalar un Macro. El código se ejecuta directamente desde el
repositorio durante el desarrollo.

Para regenerar la composición de ejemplo persistente:

```sh
fusion/shot_config/create_example_comp.sh
```

## Crear ShotConfig

Con una composición abierta, ejecutar en Fusion Console:

```lua
dofile("/Volumes/SD_02/PROYECTOS/WIP_REVIEW/fusion/shot_config/build_shot_config.lua")
```

El builder crea un único nodo visible llamado `ShotConfig`. Internamente se
identifica mediante datos persistentes propios:

```text
ShotConfig.Role          = ShotConfig
ShotConfig.SchemaVersion = 1
```

El nombre visible no es el contrato de identidad. El único recorrido de tools
que hacen los scripts sirve para localizar esta metadata; nunca se utiliza
para descubrir Loaders o Savers.

El Inspector organiza los datos en las páginas `Shot`, `Color` y `Targets`.
`Shot` reúne las secciones `Identity` y `Format`; `Targets` comienza con
`Path Map`. Los IDs técnicos
del catálogo de color se guardan como metadata persistente y no crean una
página visible de internos.

## Controles

Valores iniciales:

| Sección | Control | Default |
| --- | --- | --- |
| Identity | Show | `FOQN` |
| Identity | Episode | `E01` |
| Identity | Shot | `0010` |
| Identity | Version | `v001` |
| Targets / Path Map | Root Path Map | `_FOQN:` |
| Format | Working Resolution | `3840 × 2160` |
| Format | Crop Ratio | `2.0` |
| Format | Review Resolution | `1920 × 1080` |
| Color | Source Color Space | `Rec.709` / `REC709_COLORSPACE` |
| Color | Source Gamma | `Gamma 2.4` / `TWOPOINTFOUR_GAMMA` |
| Color | Working Color Space | `Rec.709` / `REC709_COLORSPACE` |
| Color | Working Gamma | `Linear` / `LINEAR_GAMMA` |
| Color | Embedded Alpha | desactivado |

Format y Color solo se almacenan en v0.1. Apply no los escribe en nodos de
imagen.

## Catálogo de color

Antes de crear o reconstruir ShotConfig, el builder abre una composición
privada, crea un `ColorSpaceTransform` nativo y lee las opciones reales de:

```text
InputColorSpace
InputGamma
```

La composición privada se cierra sin guardar y la composición del usuario no
se modifica. Los IDs técnicos provienen siempre de
`INPIDT_ComboControl_ID`; nunca se inventan ni se obtienen de índices de UI.

El catálogo editable usa entradas con ID persistente y etiqueta visible:

```json
{ "id": "REC709_COLORSPACE", "label": "Rec.709" }
```

La sincronización aplica estas reglas:

- las entradas del seed aparecen primero;
- una etiqueta editada en el JSON se conserva mientras exista su ID;
- un ID nuevo de Fusion se añade al final con `label = id`;
- un ID que Fusion ya no expone se elimina;
- un JSON ausente o malformado se regenera desde el seed y el CST instalado.

Las selecciones del GroupOperator se conservan por ID durante un rebuild. El
índice visible del combo no es el contrato persistente.

Para actualizar únicamente el JSON desde Terminal:

```sh
fusion/shot_config/sync_color_enums.sh
```

El runner abre Fusion Standalone 21 si no está ejecutándose, inspecciona el CST
en una comp privada y deja `color_enum_labels.json` actualizado. Para cambiar
un texto visible basta con editar `label`; `id` debe conservar el literal
exacto de Fusion.

## Configurar targets

La página `Targets` comienza con `Path Map`, seguido del botón Apply, el estado
y una ayuda breve. Debajo contiene dos grupos plegables, `Loaders` y `Savers`,
con cinco slots cada uno. El límite está definido una sola vez en
`apply_shot_config.lua`:

```lua
M.TARGET_SLOT_COUNT = 5
```

Cambiar esa constante y reconstruir ShotConfig aumenta o reduce ambos grupos
de slots. Cada slot tiene tres cajas en una disposición compacta:

```text
1 · Node Name
1 · Path Template
1 · Resolved Path
```

Ejemplo de Loader:

```text
Node Name     = MainPlate
Path Template = {root}/{show}_{episode}/BRUTOS/{show}_{episode}_{shot}.mov
```

Ejemplo de Saver:

```text
Node Name     = MasterOut
Path Template = {root}/{show}_{episode}/RENDERS/{show}_{episode}_{shot}_{version}.mov
```

Los diez slots nacen con un template de ejemplo y `Node Name` vacío. Para
activar uno, seleccionar el nodo en el Flow, pulsar F2, copiar el texto y
pegarlo en `Node Name`; después solo hay que adaptar el template. Un slot sin
nombre se ignora aunque conserve su template. Un nombre sin template es un
error y Apply aborta sin modificar ningún target. Un mismo nodo no puede
declararse en más de un slot.

`Resolved Path` es de solo lectura y contiene una expresión Lua del propio
`ShotConfig`, marcada con el prefijo `:`. Se actualiza mientras se editan el
template, Identity, Format o Color; no hace falta pulsar Apply para ver el
resultado. Apply continúa siendo la única operación que escribe la ruta en el
Loader o Saver.

Tokens soportados:

```text
{root} {show} {episode} {shot} {version}
{workingWidth} {workingHeight}
{cropX} {cropY}
{reviewWidth} {reviewHeight}
{sourceColorSpace} {sourceGamma}
{workingColorSpace} {workingGamma} {embeddedAlpha}
```

Un token desconocido o mal formado es un error y no produce escrituras.

## Aplicar

Pulsar `Apply / Update` en la página `Targets`. También se puede ejecutar desde
Console:

```lua
local apply = dofile("/Volumes/SD_02/PROYECTOS/WIP_REVIEW/fusion/shot_config/apply_shot_config.lua")
apply.run(comp)
```

El botón guarda la ruta absoluta del script del repositorio en el momento del
build. Si se mueve el checkout, basta con volver a ejecutar el builder.

Apply realiza tres fases:

1. Lee los slots activos y resuelve todos los templates.
2. Busca cada `NodeName` exacto con `comp:FindTool`, comprueba su tipo y la
   existencia de `Clip`, y captura los valores anteriores.
3. Solo después de validar el conjunto completo escribe todos los valores
   dentro de un Undo.

Si una escritura falla después de comenzar, intenta restaurar todos los
valores ya escritos y cierra el Undo como fallido. El estado corto queda en el
control `Status` y el detalle aparece en Console con el prefijo
`[ShotConfig]`.

## Fusion Path Maps

`{root}` se sustituye por el texto portable almacenado en `Root Path Map`. No
se llama a `MapPath()` para producir el valor persistente.

Con:

```text
Root Path Map = _FOQN:
Show          = FOQN
Episode       = E01
Shot          = 0010
```

y:

```text
MainPlate | {root}/{show}_{episode}/BRUTOS/{show}_{episode}_{shot}.mov
```

el Loader conserva exactamente:

```text
_FOQN:/FOQN_E01/BRUTOS/FOQN_E01_0010.mov
```

Nunca se sustituye por la ruta física local a la que apunta `_FOQN:`.

## Reconstruir durante el desarrollo

Volver a ejecutar `build_shot_config.lua`. Si encuentra una única instancia
identificada por Role:

1. captura todos los valores del contrato actual;
2. sincroniza el catálogo con el CST nativo;
3. crea por completo el nuevo GroupOperator;
4. restaura los valores y las selecciones de color por ID;
5. elimina la instancia anterior;
6. asigna la metadata estable a la nueva instancia.

La operación usa un único Undo. Si hay más de una instancia con Role
`ShotConfig`, se aborta para no elegir ni borrar una configuración ambigua.

## Tests

Desde la raíz del repositorio:

```sh
fusion/shot_config/tests/run_fusion_tests.sh
```

El runner crea una composición privada, ejecuta los casos y la cierra sin
guardar. Comprueba:

- cambio de Shot y aislamiento de nodos no registrados;
- cambio de Version solo en templates que usan `{version}`;
- aborto transaccional ante un target inexistente;
- nombres arbitrarios de nodos;
- rebuild con valores preservados y una única instancia;
- conservación del Fusion Path Map;
- aborto ante tokens desconocidos;
- validación de slots independientes de Loader y Saver;
- defaults y preservación por ID de los cuatro enums de color;
- regeneración del catálogo ante JSON malformado.

Tras comprobar una sola escritura portable con `_FOQN:`, los cambios de plano
usan el Path Map deliberadamente inexistente `_SHOTCONFIG_TEST:`. Así los tests
no abren metraje real ni muestran diálogos de duración o trimming.

Si todos los casos pasan, el runner regenera, guarda y deja activa
`examples/ShotConfig_Example.comp`. Incluye `MainPlate`, `PhoneUI`,
`MasterOut`, `ClientReview` y un `ShotConfig` con esos cuatro targets ya
rellenados. Usa `_SHOTCONFIG_TEST:` como `Root Path Map`, por lo que se puede
pulsar `Apply / Update` e inspeccionar las rutas generadas sin abrir metraje
real.

La señal final de éxito es:

```text
SHOTCONFIG_FUSION_TESTS_OK
SHOTCONFIG_EXAMPLE_COMP_READY
```

## Fuera de alcance

Esta versión no implementa InputPrep, OutputPackager, selección de vista,
activación de Savers, crop o resize automático, transformaciones de color,
base de datos ni uso del OFX `wip_review` como nodo instalado. El JSON incluido
se limita a etiquetas de presentación para enums descubiertos en el CST; no es
una configuración externa de planos.
