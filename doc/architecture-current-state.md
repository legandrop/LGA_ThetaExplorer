# Estado actual de la arquitectura

## Resumen

La camara enumera archivos individuales `CameraFileInfo`; `HdrGroupingService` los convierte en
`MediaAssetGroup` para browsing, seleccion y acciones. Un HDR representa una captura.
Desde v1.006, las versiones DNG y JPG coincidentes se fusionan en un solo grupo y una tarjeta.
Los sets con un solo formato conservan su grupo independiente.

## Flujo actual

### 1. Enumeracion

`ThetaBridge.mm` recorre `camera.mediaFiles` y emite un `QList<CameraFileInfo>` plano.

Campos relevantes hoy:

- `name`
- `uti`
- `devicePath`
- `sizeBytes`
- `creationDate`
- `isVideo`
- `isRaw`

El bridge mantiene los archivos fisicos sin mezclar formatos. Puede leer hasta1MiB de cabecera
mediante ImageCaptureCore y extraer EXIF o un preview RAW con ImageIO, sin copiar el original.
El vinculo entre formatos sigue comparando archivos; EXIF identifica las series nativas lentas.

### 2. Servicio

`ThetaCameraService` conserva la lista plana y cola thumbnails por archivo. Antes de presentar el
catalogo, lee en serie las cabeceras JPEG de candidatos consecutivos separados entre2.5segundos
y5minutos que quedaron sueltos. Reutiliza los metadatos de archivos sin cambios al refrescar.
Las lecturas tienen timeout15s por archivo, descartan respuestas antiguas y comparten la exclusion
de descargas/borrados. Un fallo de metadatos conserva esas fotos separadas y avisa una sola vez.
Si una lectura vence, se cierra y reabre la sesion antes de continuar: el timeout no cancela
la peticion nativa. El catalogo recuperado conserva lo ya leido y publica los archivos que
siguen disponibles; Refresh permite reintentar los metadatos faltantes.
Al recibir el catalogo enriquecido,
`MainWindow` llama a `HdrGroupingService::buildGroups()`: agrupa cada formato, elige la toma central
JPG y fusiona las parejas HDR DNG/JPG univocas como `HdrCombinedSet`. Ese grupo conserva el ID,
fecha e indice del representativo JPG; `files` incluye ambos formatos y `bracketCount()` cuenta
exposiciones, no archivos fisicos.

La pareja exige las mismas bases de nombre en todas las exposiciones, la misma carpeta y fechas
validas por toma a hasta cinco minutos de diferencia entre formatos. Las fechas pertenecen a
archivos distintos y no tienen por que coincidir al segundo. No basta un rango igual ni una imagen parecida;
sets con listas diferentes, duplicados ambiguos o sin fechas fiables quedan sin vinculo.
Si ambos grupos tienen las mismas tomas disponibles, pueden fusionarse aunque tengan los mismos
faltantes. La agrupacion inicial por formato puede dividir una secuencia con huecos temporales;
la fusion conserva esos tramos y no reconstruye la captura original.

El bracket nativo exige ExposureMode2, exposicion finita log2(tiempo×ISO/apertura²), pasos
de al menos0.25EV en cualquier direccion, nombres estrictamente consecutivos, carpeta comun
y fechas validas hasta5minutos entre tomas. El orden programado puede subir y bajar.
Se separa una nueva captura cuando repite las tres exposiciones iniciales (tolerancia0.05EV)
o tiene una pausa superior a30segundos y a cuatro veces la
cadencia mediana de los ultimos ocho intervalos (sin historial se usa el piso de30segundos).
La pausa tambien separa restos de una o dos fotos de una captura incompleta.
Se descuenta el mayor tiempo de obturador
de las dos fotos para no confundir una exposicion larga con una pausa. Estos limites son
heuristicos: EXIF no proporciona un identificador de serie. Sin tiempos de obturador fiables,
el corte por pausa no se aplica; el reinicio del patron sigue disponible. Exposicion repetida
adyacente o metadata ausente corta la serie. El JPEG transmite solamente los campos desconocidos al DNG de la misma toma
con correspondencia unica. Se conserva la deteccion rapida del plugin para archivos sin EXIF.

Si falla una miniatura RAW de ImageCaptureCore, el servicio encola la lectura de cabecera y
reemite thumbnailReady con el preview embebido. Conserva hasta8miniaturas RAW por sesion;
no crea archivos de descarga ni cambia los contadores o grupos mostrados. Si no puede leerlo,
conserva el placeholder, registra el error y avisa; Refresh permite reintentar.

### 3. UI

`ThumbnailGridWidget` crea un tile por grupo y conserva un cursor para la navegacion por teclado.
`ThumbnailTileWidget` muestra nombre, tipo HDRI, formatos, brackets y fecha/hora en una tarjeta
neutra. `PreviewPanel` muestra la toma representativa JPG y `FileMetadataPanel` suma ambos formatos.
La seleccion multiple sigue mostrando su informacion conjunta. Detalle de geometria y gestos: `ui.md`.

Los controles se ubican arriba del Viewer y Browse utiliza toda la altura.
`ThetaCameraService::requestPreview()` copia el representante a un destino temporal propio,
comparte la cola de Download sin emitir sus señales de progreso/completado y conserva ocho
copias. El ultimo preview pendiente reemplaza los intermedios; las miniaturas esperan durante
transferencias. `PreviewPanel::showPreviewFile()` decodifica y reproyecta en un worker y descarta
resultados de otra seleccion. `DualFisheyePreview` identifica EXIF/RICOH antes de convertir;
el original queda arriba y el panorama aproximado debajo. Las imagenes ya unidas no se deforman.

### 4. Acciones

`downloadFiles()` y `deleteFiles()` reciben `QList<CameraFileInfo>`.

`MainWindow` expande solamente los grupos seleccionados a sus archivos fisicos. Descargar o borrar
un HDR combinado incluye las tomas de ambos formatos. `HdrGroupingService::downloadGroups()`
reconstruye los grupos por formato para conservar las carpetas _jpg/_dng y sus fechas originales.
Estado local, Skip Existing y Replace usan los mismos destinos. Tras el borrado se reconstruye el
catalogo desde los archivos restantes, incluido un formato superviviente en caso de borrado parcial.

## Referencias

- `include/thetaexplorer/MediaAssetGroup.h`: captura combinada y cantidad de brackets.
- `src/services/HdrGroupingService.cpp`: `buildGroups()`, comprobacion de las mismas tomas y `downloadGroups()`.
- `src/ui/ThumbnailGridWidget.cpp`: seleccion y layout por alturas reales.
- `src/ui/ThumbnailTileWidget.cpp`: tarjeta compacta con informacion de ambos formatos.
- `src/ui/MainWindow.cpp`: catalogo y expansion de grupos para descargar/borrar.
- `src/services/ThetaCameraService.cpp`: cola serial, cache temporal y rutas de preview.
- `src/ui/PreviewPanel.cpp`: worker, seleccion vigente y dos areas de imagen.
- `src/utils/DualFisheyePreview.cpp`: proyeccion EXIF/RICOH y reproyeccion equidistante.
