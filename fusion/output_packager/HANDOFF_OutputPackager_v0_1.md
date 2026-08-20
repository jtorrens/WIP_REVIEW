# Handoff — OutputPackager v0.1 para Fusion

## Estado

Contrato de implementación de la fase posterior a `InputPrep v0.1`.

```text
fusion/
├── shot_config/
├── input_prep/
└── output_packager/
```

OutputPackager consume el contrato público de ShotConfig y una imagen mediante
conexión. Usa WIP Review únicamente como nodo OFX instalado en Fusion. No
importa, enlaza, compila ni inspecciona el código fuente del plugin.

## Objetivo

Preparar ramas de salida reproducibles y conectarlas a Savers explícitos:

```text
imagen final
→ OutputPackager
→ Saver registrado
```

Cada rama puede conservar el raster de entrada o generar el raster exacto de
review, y puede añadir WIP Review después de materializar ese raster.

## Decisiones cerradas

1. Habrá un `OutputPackager` por rama de salida.
2. Cada `OutputPackager` será un `GroupOperator` con una entrada y una salida.
3. Un único `OutputPackagerConfig` tendrá cinco filas como constante del
   builder. Cada fila declara exactamente un nombre de OutputPackager y un
   nombre de Saver; no habrá descubrimiento de targets.
4. ShotConfig continúa siendo el único propietario de paths y templates. Este
   módulo no escribe `Saver.Clip`.
5. `Review Resolution` se copia de ShotConfig mediante Apply y queda escrita
   explícitamente dentro de cada OutputPackager.
6. El framing de review es fit centrado sobre canvas negro opaco. No recorta ni
   deforma la imagen.
7. WIP Review se ejecuta después de crear el raster de review y usa `Host
   Raster` con placement `Identity`.
8. Activar WIP exige activar Review Raster. Una combinación WIP sin Review
   Raster es inválida y Apply la rechaza.
9. Los caminos no usados se omiten con nodos `Switch`; no se simula bypass con
   `Blend=0`.
10. Apply valida todos los targets antes de escribir y hace rollback completo
    si falla una escritura.
11. Las expresiones sólo pueden conectar controles con internals del mismo
    GroupOperator. No habrá expresiones entre ShotConfig, OutputPackagerConfig,
    OutputPackager o Saver.

## Componentes

### OutputPackager

Metadata:

```text
OutputPackager.Role          = OutputPackager
OutputPackager.SchemaVersion = 1
```

Grafo interno:

```text
MainInput1 ─────────────────────────────┐
    │                                   │
    └→ BetterResize → black canvas Merge│
                                        ↓
                              ReviewRasterSwitch
                                        │
                   ┌────────────────────┤
                   │                    ↓
                   │             WIP Review OFX
                   │                    │
                   └────────────→ WIPSwitch
                                        │
                                  MainOutput1
```

`ReviewRasterSwitch` selecciona entrada original o raster de review.
`WIPSwitch` selecciona imagen limpia o resultado del OFX.

Controles públicos, página `Output`:

| ID | UI | Default |
| --- | --- | --- |
| `OP_EnableReviewRaster` | Review Raster | on |
| `OP_EnableWIP` | WIP Review | off |
| `OP_Status` | Status | `Not applied` |

Página `WIP`:

- los seis textboxes de zonas;
- blanking enable, aspect y opacity;
- tipografía, fill, outline y shadow relevantes;
- frame/timecode base y FPS;
- color mode y manual color space.

Sólo se exponen controles de producción del OFX. Los diagnósticos del probe,
custom RoD y placement no forman parte de la UI: OutputPackager fija esos
valores internamente.

Página `Applied`, sólo lectura:

| ID | Origen |
| --- | --- |
| `OP_ReviewResolution` | `ShotConfig.SC_ReviewResolution` |
| `OP_CropRatio` | `ShotConfig.SC_CropRatio` para blanking custom |

El GroupOperator puede usar expresiones internas para que la UI de WIP se vea
en tiempo real. Los valores de `Applied` se escriben mediante Apply y no se
enlazan a ShotConfig.

### OutputPackagerConfig

Metadata:

```text
OutputPackagerConfig.Role          = OutputPackagerConfig
OutputPackagerConfig.SchemaVersion = 1
```

Cinco filas constantes:

```text
Package 1: [OutputPackager name] [Saver name]
...
Package 5: [OutputPackager name] [Saver name]
```

Cada fila añade tres decisiones operativas:

```text
Enabled | Review Raster | WIP Review
```

Los nombres son textboxes. El usuario copia el nombre real con F2 y lo pega.
Una fila sin ambos nombres se considera vacía; una fila parcialmente rellena es
inválida.

Controles globales:

```text
Apply / Update
Status
```

## Contrato con Saver

El Saver permanece fuera del GroupOperator y debe estar conectado a la salida
del OutputPackager declarado en la misma fila. Apply valida:

- ambos nodos existen y tienen la Role/tipo correctos;
- el Saver tiene input de imagen;
- la conexión procede del OutputPackager asociado;
- el Saver está registrado también en ShotConfig si necesita path gestionado.

`Enabled` controla el estado operativo nativo del Saver mediante
`TOOLB_PassThrough`: `false` renderiza y `true` desactiva el Saver. No se usa
`Blend` como sustituto.

## Apply / Update

Fases:

1. localizar exactamente un ShotConfig schema 1 y un OutputPackagerConfig
   schema 1;
2. leer las cinco filas declaradas;
3. resolver targets sólo con `comp:FindTool(name)`;
4. validar tipos, pares, conexiones, combinación Review/WIP y controles;
5. capturar todos los valores actuales;
6. escribir resolución, crop, switches, OFX y estado operativo de Savers;
7. releer y verificar;
8. ante cualquier fallo, restaurar todos los valores capturados.

No llama automáticamente a `apply_shot_config.lua`: paths y preparación de
outputs son operaciones explícitas separadas.

## Rebuild

Los builders serán repetibles. Al reconstruir conservarán:

- nombre y posición;
- valores públicos compatibles;
- entrada y todos los consumidores de salida;
- las cinco filas del config;
- conexión entre cada OutputPackager y Saver.

No conservarán parámetros retirados ni rutas de schema anteriores.

## Tests obligatorios

1. inventario de RegIDs y controles en Fusion Standalone 21;
2. raster 2:1 centrado sin recorte en 16:9 y output exacto;
3. bypass Review y WIP mediante Switch;
4. WIP recibe exactamente Review Resolution;
5. cinco pares explícitos sin descubrimiento;
6. fila parcial, tipo incorrecto y conexión incorrecta abortan sin cambios;
7. WIP sin Review Raster aborta;
8. enable/disable nativo del Saver no renderiza el target desactivado;
9. rollback restaura packagers y Savers;
10. rebuild conserva conexiones y valores;
11. guardar/reabrir confirma ausencia de expresiones entre componentes;
12. comp de ejemplo persistente queda abierta al finalizar.

## Fuera de alcance

- escritura de paths o templates;
- creación automática de Savers no declarados;
- render queue o lanzamiento de renders;
- selección automática por nombre o tipo;
- publicación global de macros;
- base de datos;
- dependencia del source/build del OFX;
- integración con Resolve Edit o Color;
- múltiples schemas o compatibilidad con contratos retirados.

## Definition of Done

- builders repetibles para ambos componentes;
- cinco filas exactas y configurables como constante;
- raster de review exacto y sin pérdida de encuadre;
- bypasses mediante Switch;
- WIP Review usado sólo como nodo instalado;
- Savers externos validados y activados por mecanismo nativo;
- Apply transaccional y sin discovery de targets;
- ningún enlace vivo entre dominios;
- doce pruebas pasan en Fusion Standalone 21;
- ejemplo persistente y README permiten repetir la validación.
