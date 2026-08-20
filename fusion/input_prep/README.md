# InputPrep v0.1

Implementación v0.1 para Fusion Standalone 21. Crea procesadores `InputPrep`,
un registro explícito `InputPrepConfig` y aplica los valores de `ShotConfig`
de forma transaccional. No instala macros globales.

## Probar la composición

Fusion 21 debe estar instalado en:

```text
/Applications/Blackmagic Fusion 21/Fusion.app
```

Para regenerar y abrir la comp persistente:

```sh
./fusion/input_prep/create_example_comp.sh
```

La composición queda guardada en:

```text
fusion/input_prep/examples/InputPrep_Example.comp
```

Selecciona `InputPrep1` y pulsa `1` o `2` para mostrar su salida en un Viewer.
La fuente de prueba es un `Background` cuadrado de cuatro esquinas a
2160 × 2160. El resultado permite ver el fill y el crop sobre una fuente cuyo
aspect ratio no coincide con el working raster.

La comp incluye `ShotConfig`, `InputPrepConfig` e `InputPrep1`. En la pestaña
`Targets` del registro, cada slot acepta el nombre exacto de un procesador. F2
copia el nombre del nodo. `Apply / Update` valida primero todos los slots y
solo después actualiza los targets.

## Grafo de InputPrep

```text
Input
  → [ChangeDepth]                                        → Switch
  → [AlphaDivide → ColorSpaceTransform → AlphaMultiply] → Switch
  → [BetterResize]                                      → Switch
  → [Crop]                                              → Switch
  → [ChannelBoolean: opaque alpha]                      → Switch
  → Output
```

Cada `Switch` recibe también la salida sin procesar de la etapa anterior. Al
seleccionar esa entrada, Fusion no solicita la rama transformada. Los cinco
selectores se publican en la página `InputPrep` como:

- `Change Depth`, acompañado por el valor nativo `Depth`;
- `Color Transform`;
- `Resize`;
- `Crop`;
- `Use Embedded Alpha`.

Cambiar cualquiera de ellos actúa directamente sobre `Switch.Source`: no
ejecuta Lua, no reconstruye el grupo y queda persistido en la comp.

`BetterResize` conserva el aspecto y usa el ancho como dimensión rectora. El
builder calcula el menor ancho par cuya altura derivada cubre el working
raster completo. Después `Crop` obtiene el mayor rectángulo centrado del ratio
pedido. No se introducen barras ni se hace resize no uniforme.

Configuración aplicada por defecto:

- working raster: 3840 × 2160;
- depth: valor nativo `3` de `ChangeDepth`;
- crop centrado: 2.0, con output 3840 × 1920;
- color: `REC709_COLORSPACE / TWOPOINTFOUR_GAMMA` a
  `REC709_COLORSPACE / LINEAR_GAMMA`;
- alpha: forzado a opaco porque `Embedded Alpha` es falso.

Los valores son explícitos. No hay expresiones ni enlaces vivos a
`ShotConfig`.

La pestaña `Applied` de cada `InputPrep` muestra los IDs de color/gamma y las
dimensiones escritas por el último Apply. Los bypass operativos permanecen en
la pestaña `InputPrep`.

## Validación

Con la comp del ejemplo abierta:

```sh
./fusion/input_prep/tests/run_example_test.sh
```

La geometría con fuentes de distinto aspect ratio se valida en el host con:

```sh
./fusion/input_prep/tests/run_resize_test.sh
```

El registro, rebuild y Apply/rollback se validan con:

```sh
./fusion/input_prep/tests/run_config_apply_test.sh
```

Color y alpha se comparan píxel a píxel contra grafos nativos de referencia:

```sh
./fusion/input_prep/tests/run_color_alpha_test.sh
```

El rebuild de un procesador seleccionado se valida con:

```sh
./fusion/input_prep/tests/run_rebuild_test.sh
```

El test comprueba en el host la identidad del grupo, su conexión, el estado
público, los cinco selectores y los valores serializados de depth, resize,
crop y alpha. El segundo test renderiza fuentes cuadrada, panorámica y vertical
y comprueba las dimensiones antes y después del crop. El tercero aplica a dos
targets, ignora uno no registrado, actualiza valores, conserva el registro al
reconstruirlo y comprueba validación previa y rollback. El cuarto renderiza
EXR de las políticas Opaque y Preserve y compara sus píxeles con ramas nativas
equivalentes. El quinto reconstruye un procesador mediante el builder y
comprueba identidad, valores, registro, conexiones y render posterior.

`probe_input_prep.lua` documenta los RegIDs y controles disponibles en la
instalación actual de Fusion. Crea una comp privada sin guardar:

```sh
"/Applications/Blackmagic Fusion 21/Fusion.app/Contents/Libraries/fuscript" \
  -l lua fusion/input_prep/probe_input_prep.lua
```

## Rebuild

Ejecutar `build_input_prep.lua` con un `InputPrep` seleccionado reconstruye
únicamente ese procesador. Conserva su nombre, posición, valores aplicados,
entrada, todos los consumidores de salida y los nombres almacenados en
`InputPrepConfig`. Si no hay un procesador seleccionado, crea uno nuevo.

Los nueve casos obligatorios del handoff están cubiertos con los nombres y la
metadata finales de v0.1.
