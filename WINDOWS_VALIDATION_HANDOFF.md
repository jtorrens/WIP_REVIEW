# Handoff — WIP Review OFX en Windows

Este documento es la única instrucción operativa para Windows. El alcance es exclusivamente el OFX WIP Review: ShotConfig, InputPrep, `fusion/` y sus ramas son proyectos independientes y no se deben leer, modificar, construir, probar ni integrar.

## Flujo de trabajo

El repositorio es público y GitHub Actions compila, prueba y empaqueta el OFX en Windows y macOS en cada push de las ramas OFX. Por tanto, la primera prueba en Windows no debe compilar localmente: debe descargar el bundle Windows de una ejecución verde y concentrarse en los dos hosts que CI no puede ejecutar.

- workflow: https://github.com/jtorrens/WIP_REVIEW/actions/workflows/build.yml;
- primera ejecución verde de referencia: https://github.com/jtorrens/WIP_REVIEW/actions/runs/32393291653;
- artefacto requerido: `WIPReviewProbe-windows-x64`;
- retención de artefactos: 14 días.

La CI valida MSVC x64, CTest y que el bundle carga. No valida Resolve ni Fusion, ni la negociación GPU real de sus hosts. Esas son las únicas pruebas pendientes en el equipo Windows.

## Prompt para Codex Windows

```text
Lee completamente WINDOWS_VALIDATION_HANDOFF.md y AGENTS.md antes de actuar.
Trabaja únicamente en codex/windows-opencl-validation. Actualiza esa rama remota existente; no crees una rama desde main ni codex/v1-hardening.
Para la primera validación descarga WIPReviewProbe-windows-x64 de una ejecución verde de GitHub Actions, instala el bundle solo como último paso y prueba Resolve y Fusion. Registra resultados y logs. Si la evidencia exige corregir código Windows, hazlo solo en esta rama, publica el commit, espera su CI verde, y valida el nuevo artefacto. No añadas rutas legacy, compatibilidad anterior ni fallbacks no especificados. No mezcles, no hagas force-push y no toques ShotConfig, InputPrep ni fusion/. Al finalizar, publica commits, resultados, logs resumidos y el diff exacto para revisión macOS; no integres la rama.
```

## Obtener el estado actual

Usar un clon local nuevo, fuera de OneDrive, Dropbox u otro directorio sincronizado:

```powershell
New-Item -ItemType Directory -Force C:\Codex | Out-Null
git clone https://github.com/jtorrens/WIP_REVIEW.git C:\Codex\WIP_REVIEW-Windows
Set-Location C:\Codex\WIP_REVIEW-Windows
git fetch origin --prune
git switch --track origin/codex/windows-opencl-validation
git pull --ff-only origin codex/windows-opencl-validation
git status --short --branch
git rev-parse HEAD
```

Si ya existe el clon, ejecutar solamente desde él los cuatro últimos comandos. No trabajar directamente en `main` ni `codex/v1-hardening`, no usar force-push, no añadir `fusion/` y publicar únicamente `codex/windows-opencl-validation`.

## Instalar el artefacto de CI

1. Abrir una ejecución verde de la rama Windows en Actions y descargar `WIPReviewProbe-windows-x64`.
2. Extraerlo y verificar que contiene `WIPReviewProbe.ofx.bundle\Contents\Win64\WIPReviewProbe.ofx`.
3. Crear un log explícito:

```powershell
New-Item -ItemType Directory -Force C:\Codex\WIPReviewLogs | Out-Null
[Environment]::SetEnvironmentVariable(
  "WIPREVIEW_PROBE_LOG",
  "C:\Codex\WIPReviewLogs\WIPReviewProbe.log",
  "User"
)
```

4. Confirmar que Resolve, Fusion y procesos auxiliares están cerrados.
5. Como último paso antes del ciclo de host, copiar la carpeta `WIPReviewProbe.ofx.bundle` a `C:\Program Files\Common Files\OFX\Plugins\` desde PowerShell elevado.
6. No reconstruir, sustituir el bundle ni abrir otro OFX entre esa instalación y la prueba del host.

## Validación de host requerida

Registrar versión de Windows, Resolve, Fusion, GPU y driver. Crear instancias nuevas del OFX: no cargar ajustes guardados con contratos anteriores.

### Resolve

- probar en Edit y Color con clip UHD o mayor en timeline HD;
- verificar el efecto único `WIP Review`;
- probar texto libre y campos calculados en al menos TL y BR;
- confirmar que las seis zonas son anclajes independientes del frame, que no acortan texto ni redistribuyen espacio y que el usuario controla overflow;
- comprobar blanking semitransparente, outline, shadow y los modos de composición disponibles;
- guardar un still a resolución completa y los registros `CALCULATED_FIELDS`, `CALCULATED_FIELD_ZONE`, `TEXT_ZONE`, `RENDER_BACKEND` y, si aparece, `GPU_RENDER`.

### Fusion Standalone

- usar source no-HD `4608×3164`, Request Review Raster `1920×1080` y contexto General;
- activar `Settings → Use plugin RoD for output size` (`AllowResize=1`);
- confirmar raster real `1920×1080`, los anclajes libres y que Fit no se vuelve crop;
- probar contexto Filter si está disponible;
- registrar CPU/OpenCL y comparar con CPU si Fusion expone una cola OpenCL.

No implementar un selector CPU por nodo ni una emulación de buffers. En Windows,
usar CUDA u OpenCL exclusivamente cuando el host entregue el backend y la cola o
stream correspondientes; si no entrega una ruta GPU, registrar las capacidades
exactas y usar la ruta CPU.

## Si se requiere una corrección Windows

Modificar solo lo que demuestre la evidencia, en `codex/windows-opencl-validation`. Después:

1. ejecutar CMake/MSVC x64 y CTest localmente;
2. hacer commit pequeño y push;
3. esperar la ejecución verde de GitHub Actions;
4. descargar su nuevo artefacto Windows;
5. repetir instalación como último paso y la prueba de host.

No cambiar la geometría compartida de anclajes para compensar métricas de DirectWrite. El diagnóstico actual, que aún requiere confirmación mediante un still y log nuevos, está en `handoffs/windows-readonly-directwrite-analysis.md`.

## Devolución

Añadir `handoffs/windows-validation-results.md` con:

```text
Branch y commit probado:
URL de ejecución y artefacto CI:
Windows / GPU / driver:
Build/CTest (solo si hubo cambios):
Resolve Edit y Color:
Fusion General y Filter:
Registros CPU/OpenCL:
Still a resolución completa:
FPS con blanking semitransparente:
Commits y paths modificados:
Limitaciones conocidas:
```

Publicar la rama sin integrarla. macOS revisará los cambios cuando la evidencia Windows esté completa.
