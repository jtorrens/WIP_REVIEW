# GPU backend — negociación inicial

Validación realizada el 20 de agosto de 2026 con Fusion Studio 21.0.4 y OFX
1.5.1.

## Contrato de procesamiento

El inspector expone una página **Processing** con:

- **Host GPU**: capacidades publicadas por el host;
- **CPU Only**: fuerza la ruta CPU para diagnóstico y ejecución determinista.

`CPU Only` es una selección de backend del contrato actual. No conserva una
ruta de una versión anterior ni introduce compatibilidad legacy.

Antes de acceder a `kOfxImagePropData`, Render comprueba
`MetalEnabled`, `CudaEnabled` y `OpenCLEnabled`. Un buffer GPU no implementado
se rechaza mediante `kOfxStatGPURenderFailed`, permitiendo que el host solicite
una repetición CPU sin interpretar un handle GPU como memoria convencional.

## Matriz observada

| Host | Metal | CUDA | OpenCL buffers | OpenCL images |
|---|---:|---:|---:|---:|
| Fusion Studio 21.0.4, macOS | Sí | No publicado | No publicado | No publicado |

El resultado impide adoptar OpenCL como único backend multiplataforma. El
backend macOS debe ser Metal. Windows deberá seleccionar CUDA u OpenCL según
las propiedades publicadas por Resolve/Fusion en esa plataforma.

## Base Windows

- bundle OFX en `Contents/Win64`;
- exports `dllexport`;
- rasterización UTF-8 con DirectWrite;
- paquete CPack `Windows-x64`;
- CI MSVC definida en GitHub Actions.

La ejecución de CI Windows quedó pendiente porque GitHub no inició ningún
runner: la cuenta informa pagos fallidos o límite de gasto insuficiente. No es
un fallo de compilación registrado.

## Siguiente implementación

La primera ruta GPU cubrirá Host Raster/Identity, blanking y las capas de texto
en un solo kernel lineal. Los placements que requieran resampling continuarán
solicitando CPU hasta disponer de un kernel equivalente validado.
