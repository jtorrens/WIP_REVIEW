# OutputPackager v0.1

Módulo posterior a `InputPrep`. Esta fase consumirá imágenes por conexiones y
aplicará explícitamente la configuración de salida almacenada en `ShotConfig`.
La implementación se desarrolla en esta rama sin importar, enlazar ni compilar
el código fuente del OFX WIP Review.

Los nombres y el layout interno siguen la convención común de
[`fusion/README.md`](../README.md): `Tipo_Función` y flow de izquierda a derecha.

## Checkpoint 1 — inventario del host

Antes de cerrar el contrato del componente se comprueban los nodos reales de
Fusion Standalone 21 que formarán el grafo:

- `BetterResize` para materializar `Review Resolution`;
- `Switch` para bypass sin coste del camino no seleccionado;
- `Saver` como destino externo explícito;
- WIP Review General como nodo OFX ya instalado.

La variante `.Filter` se registra sólo como dato diagnóstico. Fusion Standalone
21 no la expone en el host actual y OutputPackager no depende de ella.

Ejecutar:

```sh
fusion/output_packager/run_probe.sh
```

La sonda crea una composición privada no guardada, imprime RegIDs, controles y
combos relevantes, y la cierra al terminar. El checkpoint sólo pasa
si existen todos los bloques requeridos y termina con:

```text
OUTPUTPACKAGER_PROBE_READY
OUTPUTPACKAGER_HOST_PROBE_OK
```

El RegID del OFX se trata como contrato runtime de Fusion. Ningún archivo de
`src/`, `include/`, `tests/` o del build del plugin será dependencia de este
módulo.

## Checkpoint 2 — raster de review

El camino validado en el host es:

```text
imagen preparada
→ BetterResize (fit centrado)
→ Merge sobre Background negro opaco a Review Resolution
→ WIP Review en Host Raster / Identity
```

`BetterResize` con `KeepAspect=1` conserva la imagen completa, pero no genera
por sí solo un canvas con las dimensiones solicitadas cuando cambia el aspect
ratio. Por ejemplo, una entrada 2:1 produce `1920 × 960` al pedir
`1920 × 1080`. El `Background + Merge` materializa el raster exacto, centra la
imagen y evita tanto recorte como deformación.

WIP Review recibe ya el raster definitivo. Por ello OutputPackager no depende
de `Request Custom Output RoD` ni de `Use plugin RoD for output size`; usa
`Host Raster`, `Identity` y conserva exactamente `Review Resolution`.

Ejecutar la prueba reducida equivalente a `400 × 200 → 192 × 108`:

```sh
fusion/output_packager/tests/run_review_raster_test.sh
```

La prueba cierra su comp temporal y termina con:

```text
OUTPUTPACKAGER_REVIEW_RASTER_HOST_TEST_OK
```

## Checkpoint 4 — activación nativa de Savers

Fusion 21 omite durante el render un Saver con:

```lua
saver:SetAttrs({ TOOLB_PassThrough = true })
```

OutputPackager usará ese atributo nativo. Un Saver habilitado tendrá
`TOOLB_PassThrough=false`; nunca se utilizará `Blend=0` para fingir que está
desactivado.

La prueba renderiza dos Savers conectados a la misma imagen y comprueba en
disco que sólo el habilitado crea su secuencia:

```sh
fusion/output_packager/tests/run_saver_enable_test.sh
```

Resultado:

```text
OUTPUTPACKAGER_SAVER_ENABLE_HOST_TEST_OK
```

## Checkpoint 5 — componente de imagen

`build_output_packager.lua` crea un `GroupOperator` conectado con:

```text
Input
→ centered fit + opaque review canvas
→ ReviewRaster Switch
→ WIP Review General
→ WIP Switch
→ Output
```

El canvas contiene los valores explícitos de Review Resolution. BetterResize
lee internamente ese canvas, por lo que su altura efectiva reducida nunca puede
alterar las dimensiones finales. Los dos bypasses son nodos `Switch`.

Con una comp abierta, crear una instancia:

```lua
dofile("/Volumes/SD_02/PROYECTOS/WIP_REVIEW-output-packager/fusion/output_packager/build_output_packager.lua")
```

Prueba de host:

```sh
fusion/output_packager/tests/run_builder_test.sh
```

Comprueba en un consumidor externo los tres estados: source raster `400×200`,
review limpio `192×108` y review con OFX `192×108`. Al terminar cierra la comp
temporal y muestra:

```text
OUTPUTPACKAGER_BUILDER_HOST_TEST_OK
```

## Checkpoint 6 — registro y Apply

`OutputPackagerConfig` contiene cinco pares constantes:

```text
OutputPackager Node | Saver Node | Enabled | Review Raster | WIP Review
```

Los nombres son textboxes para copiar con F2. Una pareja vacía se ignora; una
pareja parcial, duplicada o con tipos/conexiones incorrectos aborta antes de
modificar ningún target.

Crear o reconstruir el registro:

```lua
dofile("/Volumes/SD_02/PROYECTOS/WIP_REVIEW-output-packager/fusion/output_packager/build_output_packager_config.lua")
```

`Apply / Update` copia Review Resolution y Crop Ratio desde ShotConfig, fija los
dos Switches de cada OutputPackager y activa o desactiva su Saver con
`TOOLB_PassThrough`. La operación captura y verifica GroupOperators y Savers;
un fallo restaura ambos dominios.

Prueba de aceptación:

```sh
fusion/output_packager/tests/run_config_apply_test.sh
```

Valida dos paquetes, un tercero no registrado, rebuild, pareja parcial, WIP sin
review, conexión incorrecta y rollback forzado. La comp temporal se cierra al
terminar:

```text
OUTPUTPACKAGER_CONFIG_APPLY_HOST_TEST_OK
```

## Checkpoint 7 — rebuild conectado

Ejecutar el builder con un OutputPackager seleccionado reconstruye esa misma
instancia. Conserva:

- nombre y posición;
- todos los controles públicos;
- conexión de entrada;
- todos los consumidores de salida, incluido el Saver;
- metadata Role y SchemaVersion.

No deja la instancia anterior ni crea un segundo processor. Prueba:

```sh
fusion/output_packager/tests/run_rebuild_test.sh
```

La comp temporal se cierra y el resultado es:

```text
OUTPUTPACKAGER_REBUILD_HOST_TEST_OK
```

## Checkpoint 8 — UI de WIP Review

La página `WIP` expone únicamente controles de producción del nodo instalado:

- blanking y opacity;
- font, tamaño, opacity, outline y shadow;
- seis zonas con enable, textbox y calculated field;
- frame relative base, frame start, FPS y timecode;
- color-space mode y manual color space.

`Canvas Mode`, placement, custom RoD, `AllowResize` y diagnósticos permanecen
ocultos y fijados por el grafo. Crop Ratio continúa en `Applied` y alimenta
internamente el blanking custom sin expresión hacia ShotConfig.

La prueba escribe texto y tokens en zonas opuestas, configura timing/color y
reconstruye el Group para comprobar persistencia:

```sh
fusion/output_packager/tests/run_wip_ui_test.sh
```

Resultado:

```text
OUTPUTPACKAGER_WIP_UI_HOST_TEST_OK
```

## Checkpoint 9 — ejemplo persistente

Generar, guardar y dejar abierta la comp base:

```sh
fusion/output_packager/create_example_comp.sh
```

`examples/OutputPackager_Example.comp` incluye:

- fuente final 2:1 con patrón de esquinas;
- `Group_OutputPackager_ClientReview` con WIP y tokens frame/timecode;
- `Group_OutputPackager_CleanReview` sin overlay;
- `Saver_ClientReview` y `Saver_CleanReview` habilitados;
- ShotConfig con templates portables bajo `_OUTPUTPACKAGER_TEST:`;
- OutputPackagerConfig con las dos parejas ya registradas.

La comp se guarda antes de quedar abierta. Su prueba no crea otro documento:

```sh
fusion/output_packager/tests/run_example_test.sh
```

Resultado:

```text
OUTPUTPACKAGER_EXAMPLE_HOST_TEST_OK
```

Los probes y tests temporales reutilizan la comp vacía activa y la cierran desde
su runner. No acumulan tabs ni muestran una secuencia de diálogos de guardado.
