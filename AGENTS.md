# Development rules

These instructions apply to the entire repository.

## Clean-forward development

This project is in active development. Prefer a single clean implementation of
the current contract over backward compatibility.

- Do not preserve deprecated parameters, aliases, previous-version behavior,
  migration shims, compatibility adapters, or legacy render paths.
- When a contract is replaced, remove the superseded code, tests, scripts,
  documentation, labels, and logging rather than hiding or disabling them.
- Do not add fallbacks whose purpose is to load or emulate an earlier project
  version.
- A fallback is allowed only when the current source-of-truth specification
  explicitly requires it for runtime behavior, such as unavailable-font
  handling or the documented Host Raster capability path. Such a fallback must
  implement only the current contract and must not retain a deprecated route.
- Before each checkpoint, search the repository for stale version labels,
  deprecated parameter names, compatibility language, and unreachable legacy
  branches; remove every occurrence within the checkpoint's scope.
