# Plan de implementacion recomendado

## Fase 1. Modelo

Agregar una capa de dominio nueva:

- `MediaAssetGroup.h`
- `HdrGroupingService.h/.cpp` o similar

Responsabilidades:

- recibir `QList<CameraFileInfo>`
- devolver `QList<MediaAssetGroup>`
- encapsular toda la heuristica HDR

## Fase 2. UI por grupos

Cambiar la grilla para que renderice `MediaAssetGroup` en lugar de `CameraFileInfo`.

Impacto esperado:

- `ThumbnailGridWidget`
- `ThumbnailTileWidget`
- `MainWindow`
- `PreviewPanel`
- `FileMetadataPanel`

## Fase 3. Thumbnails

Pedir thumbnail solo del `representative.devicePath` de cada grupo.

Ventajas:

- menos trafico contra la camara
- menos carga sobre ImageCaptureCore
- mejor tiempo de respuesta del browser

## Fase 4. Acciones

Mantener `ThetaCameraService` y `ThetaBridge` trabajando con archivos reales.

Solo cambiar:

- la seleccion en `MainWindow`
- el aplanado de grupos antes de descargar
- el aplanado de grupos antes de borrar

Esto minimiza riesgo porque no toca el bridge salvo que sea necesario.

## Fase 5. Metadata de set

Agregar una vista resumida para grupos:

- tipo de set
- cantidad de imagenes
- tamano total
- rango temporal
- nombre representativo

## Fase 6. Ajuste fino de heuristica

Agregar logging y un modo debug simple para inspeccionar agrupacion:

- archivos que se agruparon
- score obtenido
- razon por la que un candidato quedo como single

Eso te va a permitir calibrar rapido con material real de la Z1.

## Orden recomendado de trabajo

1. crear tipos nuevos
2. implementar grouping puro y testeable
3. adaptar la grilla a grupos
4. adaptar preview y metadata
5. adaptar download/delete
6. recien al final pulir labels y visuales

## Decision tecnica importante

La heuristica HDR deberia vivir fuera de la UI y fuera del bridge.

Motivo:

- el bridge solo habla con la camara
- la UI solo consume assets preparados
- la logica de agrupacion necesita poder testearse con listas simuladas
