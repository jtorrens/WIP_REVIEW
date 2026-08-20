# Convenciones de nodos Fusion

Estas reglas se aplican a todos los grupos y macros generados por este árbol:

- cada nombre visible empieza por el tipo real del nodo: `Tipo_Función`;
- los `GroupOperator` empiezan por `Group_`;
- el camino principal se dispone de izquierda a derecha sobre `Y = 0`;
- las ramas de proceso se colocan debajo y las fuentes auxiliares encima;
- cada rama vuelve a su `Switch` antes de comenzar la etapa siguiente.

Ejemplos: `ChangeDepth_Working`, `ColorSpaceTransform_Working`,
`BetterResize_ReviewRaster`, `Switch_WIP` y `Group_OutputPackager_ClientReview`.

Los IDs de controles públicos (`IP_*`, `OP_*`, `SC_*`) se mantienen separados
del nombre visible del nodo: identifican el contrato de datos, no el tipo.
