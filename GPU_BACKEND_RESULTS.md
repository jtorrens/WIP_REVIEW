# GPU backend — contrato multiplataforma

Validación macOS realizada el 20 de agosto de 2026 con Fusion Studio 21.0.4
y OFX 1.5.1.

## Selección de backend

El plugin publica la aceleración nativa de cada plataforma y el host decide el
tipo de buffer antes de llamar a Render:

- macOS: Metal buffers;
- Windows: CUDA streams y buffers OpenCL 1.1;
- CPU: memoria convencional cuando el host no activa GPU.

Render comprueba `MetalEnabled`, `CudaEnabled` y `OpenCLEnabled` antes de
interpretar `kOfxImagePropData`. No se interpreta nunca un handle GPU como un
puntero CPU. Cada binario anuncia únicamente los backends que implementa.

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

## Windows / CUDA y OpenCL

El backend CUDA usa el stream OFX entregado por el host y se compila de forma
explícita con `-DWIPREVIEW_ENABLE_CUDA=ON`. El bundle genérico Win64 se
construye con esa opción desactivada y usa CPU u OpenCL según negocie el host.
Ambos implementan el mismo contrato visual que Metal. Los kernels OpenCL se
limitan a OpenCL C 1.1. `OpenCL.dll` se carga dinámicamente, por lo que el
bundle no distribuye ni sustituye el ICD del fabricante de la GPU.
OpenCL-Headers está aislado y fijado al commit
`4ea6df132107e3b4b9407f903204b5522fdffcd6`.

La CI pública valida MSVC, CTest, CPack y carga del bundle Win64 genérico. La
validación CUDA se realizó en una RTX 4080 Laptop GPU con CUDA Toolkit 13.3:
Resolve negoció `cuda_enabled=1`, registró `GPU_RENDER backend=cuda status=0`
y mantuvo 25 fps en un clip UHD. La validación Fusion Windows sigue pendiente.

## Matriz observada

| Host | Metal | CUDA | OpenCL buffers | OpenCL images |
|---|---:|---:|---:|---:|
| Fusion Studio 21.0.4, macOS | Sí | No publicado | No publicado | No publicado |
| Resolve, Windows RTX 4080 | No | Sí | No negociado | No negociado |
| Fusion, Windows | Pendiente | Pendiente | Pendiente | Pendiente |

## Empaquetado

- macOS: bundle universal en `Contents/MacOS`, firmado ad-hoc;
- Windows: bundle x64 en `Contents/Win64`;
- texto macOS: CoreText/CoreGraphics;
- texto Windows: DirectWrite/GDI;
- CPack: `macOS-universal` y `Windows-x64`.
