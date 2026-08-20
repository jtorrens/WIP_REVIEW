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
combos relevantes, y la deja abierta para inspección. El checkpoint sólo pasa
si existen todos los bloques requeridos y termina con:

```text
OUTPUTPACKAGER_PROBE_READY
OUTPUTPACKAGER_HOST_PROBE_OK
```

El RegID del OFX se trata como contrato runtime de Fusion. Ningún archivo de
`src/`, `include/`, `tests/` o del build del plugin será dependencia de este
módulo.
