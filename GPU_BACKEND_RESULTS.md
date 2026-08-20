# GPU backend — contrato multiplataforma

Validación macOS realizada el 20 de agosto de 2026 con Fusion Studio 21.0.4
y OFX 1.5.1.

## Selección de backend

El plugin publica la aceleración nativa de cada plataforma y el host decide el
tipo de buffer antes de llamar a Render:

- macOS: Metal buffers;
- Windows: OpenCL 1.1 buffers;
- CPU: memoria convencional cuando el host no activa GPU.

Render comprueba `MetalEnabled`, `CudaEnabled` y `OpenCLEnabled` antes de
interpretar `kOfxImagePropData`. No se interpreta nunca un handle GPU como un
puntero CPU. CUDA no se anuncia porque no existe una implementación CUDA.

No se expone un selector CPU por nodo. Fusion 21 entrega buffers Metal privados
cuando selecciona Metal, y no reevalúa de forma fiable la capacidad GPU por
instancia: modificarla desactivó Metal para todas las instancias de la sesión.
La selección forzada de CPU pertenece por tanto a la configuración GPU del
host, antes de que éste cree los buffers de Render.

## macOS / Metal

El backend Metal implementa el renderer completo:

- Identity, Fit, Fill/Crop, Stretch y 1:1;
- Bilinear, Bicubic Catmull-Rom y Lanczos3;
- Byte, Short, Half y Float RGBA premultiplicado o straight;
- Rec.709 Gamma 2.4, Rec.2100 PQ y Rec.2100 HLG;
- canvas, blanking, shadow, outline y fill en orden contractual.

Los kernels embebidos se compilan con Metal y se cachean por dispositivo. La
ruta no-Identity decodifica Source una vez a display-light-linear, remuestrea y
compone las capas antes del único encode de salida.

La prueba directa compara Metal contra CPU en 18 combinaciones de placement,
filtro y espacio de salida, con blanking y texto incluidos. El smoke acumulativo
de Fusion renderiza 33 escenarios y registra `GPU_RENDER backend=metal status=0`.

En Resolve Studio 21.0.4, el clip real usado durante el desarrollo mantiene
25 fps con blanking semitransparente. Antes del backend Metal, esa misma
configuración se reproducía aproximadamente a 6 fps. La comprobación visual y
de reproducción fue aprobada por el usuario el 20 de agosto de 2026.

## Windows / OpenCL

El backend OpenCL usa buffers OFX 1.5 y el mismo contrato visual que Metal. Los
kernels se limitan a OpenCL C 1.1. `OpenCL.dll` se carga dinámicamente, por lo
que el bundle no distribuye ni sustituye el ICD del fabricante de la GPU.
OpenCL-Headers está aislado y fijado al commit
`4ea6df132107e3b4b9407f903204b5522fdffcd6`.

El kernel OpenCL pasa compilación sintáctica offline. La compilación MSVC y la
ejecución dentro de Resolve/Fusion para Windows siguen pendientes: GitHub no
inició el runner de la workflow porque la cuenta informó pagos fallidos o un
límite de gasto insuficiente. Esto no constituye una validación Win64.

## Matriz observada

| Host | Metal | CUDA | OpenCL buffers | OpenCL images |
|---|---:|---:|---:|---:|
| Fusion Studio 21.0.4, macOS | Sí | No publicado | No publicado | No publicado |
| Resolve/Fusion, Windows | Pendiente | Pendiente | Pendiente | Pendiente |

## Empaquetado

- macOS: bundle universal en `Contents/MacOS`, firmado ad-hoc;
- Windows: bundle x64 en `Contents/Win64`;
- texto macOS: CoreText/CoreGraphics;
- texto Windows: DirectWrite/GDI;
- CPack: `macOS-universal` y `Windows-x64`.
