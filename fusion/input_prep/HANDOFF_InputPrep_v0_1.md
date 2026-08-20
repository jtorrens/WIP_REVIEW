# Handoff — InputPrep v0.1 para Fusion

## Estado

Contrato de implementación de la fase posterior a `ShotConfig v0.1`.

Esta fase se desarrolla en el mismo repositorio, pero como módulo independiente:

```text
fusion/
├── shot_config/
└── input_prep/
```

`InputPrep` depende del contrato público de `ShotConfig`; no depende del OFX de
WIP Review ni de sus nodos. Esta separación es permanente para código, ramas y
builds. Un módulo futuro podrá instanciar el OFX ya instalado dentro de una
comp de Fusion para generar WIPs, pero nunca importará, enlazará ni compilará
su código fuente.

## Objetivo

Convertir una o varias ramas de imagen al dominio de trabajo declarado por
`ShotConfig`:

```text
imagen conectada
    ↓
InputPrep
    ↓
working raster + crop + working color + alpha normalizado
```

La configuración se lee y se aplica mediante Lua. No se crearán expresiones
entre `ShotConfig` e `InputPrep`.

## Decisiones cerradas

1. Habrá un `InputPrep` por rama de imagen.
2. Cada `InputPrep` será un `GroupOperator` con una entrada y una salida de
   imagen.
3. Los targets se declararán por nombre en un único `InputPrepConfig`; no se
   descubrirán recorriendo la comp.
4. `ShotConfig v0.1` no cambia de schema ni recibe controles nuevos.
5. `Review Resolution` no se aplica en esta fase. Pertenece al futuro
   `OutputPackager`.
6. La imagen se procesa en este orden lógico:

   ```text
   normalización de profundidad
   → tratamiento de premultiplicación
   → conversión Source Color/Gamma a Working Color/Gamma
   → resize uniforme centrado
   → crop centrado
   → normalización final de alpha
   ```

7. Los valores se escriben explícitamente en los nodos internos. No habrá
   enlaces vivos a controles de otro componente.
8. La aplicación a varios targets será transaccional: si uno falla, ninguno
   queda parcialmente actualizado.

## Componentes

### ShotConfig

Fuente de verdad existente para:

- Working Resolution;
- Crop Ratio;
- Source Color Space y Source Gamma;
- Working Color Space y Working Gamma;
- Embedded Alpha.

Se localiza únicamente mediante:

```text
ShotConfig.Role = ShotConfig
ShotConfig.SchemaVersion = 1
```

Debe existir exactamente uno.

### InputPrepConfig

Componente de datos y operación. No procesa píxeles.

Metadata:

```text
InputPrep.Role = InputPrepConfig
InputPrep.SchemaVersion = 1
```

UI pública, en una sola página `Targets`:

```text
InputPrep Target 1
InputPrep Target 2
InputPrep Target 3
InputPrep Target 4
InputPrep Target 5

Apply / Update
Status
```

Cada target es una caja de texto `Node Name`. Los slots vacíos se ignoran. El
límite se define una sola vez en Lua:

```lua
TARGET_SLOT_COUNT = 5
```

El usuario copia el nombre real del nodo con F2 y lo pega en el slot. Los
nombres pueden ser arbitrarios.

Solo puede existir un `InputPrepConfig` por comp. Su builder debe poder
reconstruirlo conservando sus cinco nombres y sin duplicarlo.

### InputPrep

Procesador de imagen reutilizable.

Metadata:

```text
InputPrep.Role = InputPrep
InputPrep.SchemaVersion = 1
```

Contrato externo:

```text
Input  — imagen sin preparar
Output — imagen en el dominio de trabajo
```

No duplica los controles públicos de `ShotConfig`. Puede exponer únicamente
un estado de solo lectura para diagnóstico y los switches operativos por
rama `Change Depth`, `Color Transform`, `Resize`, `Crop` y
`Use Embedded Alpha`. `Depth` publica además la selección nativa de
`ChangeDepth`, con valor inicial `3`. Los switches usan la herramienta nativa
`Switch` y seleccionan directamente entre la rama sin procesar y la rama
transformada; no ejecutan scripts ni usan `Blend = 0`.

Los nodos internos tendrán IDs lógicos estables almacenados como metadata o
localizados mediante identidad propia, nunca solo por su nombre visible. La
elección concreta de herramientas nativas de Fusion debe validarse en Fusion
Standalone 21 antes de fijarla.

## Semántica de imagen

### Working Resolution

`Working Resolution` define el rectángulo máximo de trabajo. La fuente se
escala uniformemente y se centra hasta cubrirlo por completo:

- se conserva el aspecto de la fuente;
- no se estira de forma no uniforme;
- puede perderse imagen fuera del rectángulo;
- no se introducen barras vacías.

Implementación validada en Fusion Standalone 21: `BetterResize` usa
`KeepAspect = 1` y toma el ancho como dimensión rectora. El builder calcula el
menor ancho par cuya altura derivada cubre también `Working Height`; el crop
centrado se aplica después. Las pruebas de host cubren fuentes cuadrada,
panorámica y vertical.

### Crop Ratio

`Crop Ratio` es `ancho / alto` y debe ser mayor que cero.

El output es el mayor rectángulo centrado con ese ratio que cabe dentro de
`Working Resolution`. Sus dimensiones se redondean a enteros pares.

Ejemplo:

```text
Working Resolution = 3840 × 2160
Crop Ratio          = 2.0
Output               = 3840 × 1920
```

No se escriben ni se usan `Review Width` o `Review Height`.

### Color

El transform utiliza los IDs técnicos persistentes que ya resuelve
`ShotConfig`:

```text
InputColorSpace  = Source Color Space ID
InputGamma       = Source Gamma ID
OutputColorSpace = Working Color Space ID
OutputGamma      = Working Gamma ID
```

No se usarán etiquetas visibles como contrato y no se traducirán IDs mediante
índices fijos.

Si source y working son idénticos, el componente puede evitar trabajo de
color, pero el resultado y la metadata aplicada deben seguir siendo
deterministas.

### Alpha

Con `Embedded Alpha = true`:

- se conserva el alpha de entrada;
- el color se transforma sin contaminar píxeles semitransparentes;
- el output queda premultiplicado.

Con `Embedded Alpha = false`:

- el alpha de salida se fuerza a `1.0`;
- no se interpreta un canal alpha accidental de la fuente.

Implementación validada con píxeles semitransparentes en Fusion 21:

- con Embedded Alpha activo, la rama usa
  `AlphaDivide → CST → AlphaMultiply` y conserva alpha;
- con Embedded Alpha desactivado, el CST recibe RGB directamente y
  `ChannelBoolean` fuerza alpha a `1.0`.

Dos switches internos siguen la política pública de alpha y evitan evaluar
divide/multiply cuando no corresponden.

## Builders y scripts

Estructura actual:

```text
fusion/input_prep/
├── build_input_prep.lua
├── build_input_prep_config.lua
├── apply_input_prep.lua
├── geometry.lua
├── README.md
└── tests/
    ├── config_apply_host_test.lua
    ├── color_alpha_host_test.lua
    ├── example_host_test.lua
    ├── resize_host_test.lua
    ├── run_config_apply_test.sh
    ├── run_color_alpha_test.sh
    ├── run_example_test.sh
    ├── rebuild_host_test.lua
    ├── run_rebuild_test.sh
    ├── run_resize_test.sh
    └── verify_color_alpha.py
```

### build_input_prep.lua

- crea un `InputPrep` dentro de la comp;
- conecta y publica una entrada y una salida;
- crea los nodos internos con identidad estable;
- no crea dependencias con nombres visuales externos;
- permite reconstruir un `InputPrep` explícitamente seleccionado conservando
  sus conexiones cuando Fusion lo permita de forma verificable.

Comportamiento validado: al ejecutar el builder con un `InputPrep`
seleccionado, se conserva nombre, posición, valores públicos, entrada y todos
los consumidores de salida. El registro no cambia porque mantiene el nombre
estable. Sin selección compatible, el builder crea un procesador nuevo.

No recorre ni reconstruye todos los `InputPrep` de la comp.

### build_input_prep_config.lua

- crea o reconstruye el único `InputPrepConfig`;
- conserva los nombres de target compatibles;
- añade `Apply / Update` y `Status`;
- aborta si encuentra múltiples configs con la misma Role.

### apply_input_prep.lua

Flujo obligatorio:

```text
leer ShotConfig e InputPrepConfig
↓
resolver valores y calcular dimensiones
↓
buscar exactamente cada Node Name con comp:FindTool()
↓
validar Role, SchemaVersion, entrada, salida y nodos internos
↓
capturar los valores actuales de todos los targets
↓
escribir todos los targets dentro de Undo
↓
verificar las escrituras
↓
rollback completo si alguna falla
```

Puede recorrer tools únicamente para localizar `ShotConfig` e
`InputPrepConfig` por Role. Está prohibido descubrir procesadores `InputPrep`
mediante `GetToolList()`.

## Validación previa

Antes de escribir se comprobará:

- exactamente un `ShotConfig` schema 1;
- exactamente un `InputPrepConfig` schema 1;
- al menos un target no vacío;
- nombres de target no duplicados;
- existencia de cada target;
- Role y SchemaVersion correctos;
- controles e internals requeridos presentes;
- Working Width y Height positivos;
- Crop Ratio positivo y compatible con un output mínimo de 2 × 2;
- IDs de color y gamma presentes en la metadata de ShotConfig;
- soporte real de esos IDs en el CST instalado.

Un error produce:

```text
[InputPrep] ERROR: ... Nothing was changed.
```

## Tests obligatorios

### A — Targets explícitos

Aplicar a dos `InputPrep` registrados y dejar un tercero sin registrar.
Solo cambian los dos declarados.

### B — Working Resolution

Cambiar `3840 × 2160` por `2048 × 1152`. Todos los targets registrados reciben
el nuevo raster; el no registrado permanece igual.

### C — Crop

Con `3840 × 2160` y ratio `2.0`, verificar output `3840 × 1920`, centrado y sin
stretch.

### D — Color

Verificar que el CST recibe exactamente los cuatro IDs seleccionados en
ShotConfig y que un píxel de prueba produce el resultado esperado.

### E — Alpha

Probar un píxel semitransparente con Embedded Alpha activo y desactivo. El
primer caso conserva alpha y premultiplicación; el segundo devuelve alpha 1.

### F — Target inválido

Un nombre inexistente o un nodo con Role incorrecta aborta antes de modificar
los demás.

### G — Rollback

Forzar un fallo durante una escritura y comprobar que se restauran todos los
valores anteriores.

### H — Rebuild

Reconstruir `InputPrepConfig` y un `InputPrep`; conservar targets y conexiones
sin crear duplicados.

### I — Ausencia de enlaces vivos

Guardar y reabrir la comp. Confirmar que los internals contienen valores
explícitos y ninguna expresión que apunte a `ShotConfig` o
`InputPrepConfig`.

## Fuera de alcance

No implementar todavía:

- Review Resolution;
- OutputPackager;
- creación o enable/disable de Savers;
- uso del WIP Review OFX como nodo instalado en runtime;
- selección de View;
- render queue;
- publicación o instalación global de macros;
- overrides de color por target;
- framing manual, pan/scan o animación;
- configuración externa o base de datos.

## Definition of Done

La fase termina cuando:

- ambos builders son repetibles y no duplican configs;
- cada `InputPrep` funciona por conexión de imagen;
- el registro contiene únicamente nombres explícitos;
- Apply no descubre procesadores recorriendo la comp;
- resolución, crop, color y alpha se escriben explícitamente;
- Review Resolution no participa;
- todos los targets se validan antes de escribir;
- un fallo restaura el estado completo;
- no existen expresiones entre dominios;
- los nueve casos obligatorios pasan en Fusion Standalone 21;
- README y comp de ejemplo permiten repetir la prueba desde el repositorio.

## Orden de implementación

1. Probar en una comp privada los RegIDs y controles nativos de resize, crop,
   CST y alpha.
2. Fijar el grafo interno mínimo con una prueba de píxeles.
3. Implementar `InputPrep` y su builder.
4. Implementar `InputPrepConfig` y el registro explícito.
5. Implementar Resolve → Validate → Apply → Verify/Rollback.
6. Añadir tests de host, comp persistente y documentación.
