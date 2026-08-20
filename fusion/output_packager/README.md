# OutputPackager v0.1

Módulo posterior a `InputPrep`. Esta fase consumirá imágenes por conexiones y
aplicará explícitamente la configuración de salida almacenada en `ShotConfig`.
La implementación se desarrolla en esta rama sin importar, enlazar ni compilar
el código fuente del OFX WIP Review.

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
