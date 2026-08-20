# InputPrep v0.1

Primer prototipo de host para Fusion Standalone 21. Todavía no es el builder
de producción ni instala macros globales: crea un `GroupOperator` real dentro
de una composición y permite validar el grafo nativo antes de implementar
`InputPrepConfig` y la aplicación transaccional a varios targets.

## Probar la composición

Fusion 21 debe estar instalado en:

```text
/Applications/Blackmagic Fusion 21/Fusion.app
```

Para regenerar y abrir la comp persistente:

```sh
./fusion/input_prep/create_prototype_comp.sh
```

La composición queda guardada en:

```text
fusion/input_prep/examples/InputPrep_Prototype.comp
```

Selecciona `InputPrep1` y pulsa `1` o `2` para mostrar su salida en un Viewer.
La fuente de prueba es un `Background` de cuatro esquinas a 3840 × 2160.

## Grafo del prototipo

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

Configuración aplicada por defecto:

- working raster: 3840 × 2160;
- depth: valor nativo `3` de `ChangeDepth`;
- crop centrado: 2.0, con output 3840 × 1920;
- color: `REC709_COLORSPACE / TWOPOINTFOUR_GAMMA` a
  `REC709_COLORSPACE / LINEAR_GAMMA`;
- alpha: forzado a opaco porque `Embedded Alpha` es falso.

Los valores son explícitos. No hay expresiones ni enlaces vivos a
`ShotConfig`.

## Validación

Con la comp del ejemplo abierta:

```sh
./fusion/input_prep/tests/run_prototype_test.sh
```

El test comprueba en el host la identidad del grupo, su conexión, el estado
público, los cinco selectores y los valores serializados de depth, resize,
crop y alpha.

`probe_input_prep.lua` documenta los RegIDs y controles disponibles en la
instalación actual de Fusion. Crea una comp privada sin guardar:

```sh
"/Applications/Blackmagic Fusion 21/Fusion.app/Contents/Libraries/fuscript" \
  -l lua fusion/input_prep/probe_input_prep.lua
```

## Pendiente antes del builder de producción

Este checkpoint valida el esqueleto y la integración con el host. La siguiente
prueba debe fijar con imágenes de distinto aspect ratio el comportamiento
exacto de `BetterResize` para obtener fill uniforme, centrado y sin stretch.
Después se implementarán `InputPrepConfig`, los cinco targets explícitos y el
flujo Resolve → Validate → Apply → Verify/Rollback descrito en el handoff.
