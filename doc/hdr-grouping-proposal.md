# Propuesta de deteccion y manejo de secuencias HDR

Documento de propuesta original. La representacion por formato fue reemplazada en v1.006
por una tarjeta por captura DNG + JPG; el estado vigente esta en `architecture-current-state.md`
y `ui.md`.

## Objetivo

Mostrar en el browser:

- un solo thumbnail por cada HDR en JPG
- un solo thumbnail por cada HDR en DNG
- un tile individual para las fotos que no pertenecen a ningun set

Y al operar sobre ese tile:

- descargar todo el set
- borrar todo el set

Mostrando ademas:

- thumbnail representativo: la toma del medio
- contador de imagenes que componen el set

## Recomendacion principal

No intentar resolver esto desde la UI ni desde el bridge.

La mejor arquitectura es:

1. mantener `CameraFileInfo` como representacion del archivo fisico real
2. crear un nuevo modelo intermedio, por ejemplo `MediaAssetGroup`
3. agrupar en memoria apenas llega `fileListReady`
4. hacer que la grilla, seleccion, preview y acciones trabajen sobre grupos

## Nuevo modelo sugerido

Ejemplo conceptual:

```cpp
enum class AssetKind {
    SinglePhoto,
    SingleRaw,
    Video,
    HdrJpegSet,
    HdrRawSet
};

struct MediaAssetGroup {
    QString id;
    AssetKind kind;
    QList<CameraFileInfo> files;
    CameraFileInfo representative;
    int representativeIndex = 0;
    int imageCount = 0;
    bool isHdrSet = false;
    bool isRaw = false;
    QDateTime captureTime;
    QString displayTitle;
};
```

Puntos clave:

- `files` contiene todos los archivos fisicos afectados por descargar/borrar
- `representative` es el archivo del medio para thumbnail y preview
- `id` debe ser estable dentro de la sesion
- JPG y DNG viven en grupos separados aunque provengan del mismo disparo HDR

## Heuristica recomendada para detectar HDR

La deteccion no deberia depender de una sola regla. Conviene una heuristica por puntaje con fallback.

### Senales fuertes

- mismo tipo base: JPG con JPG, DNG con DNG
- timestamps muy cercanos entre si
- nombres con prefijo comun y numeracion consecutiva
- tamanos y dimensiones comparables dentro del mismo set

### Senales medias

- mismo directorio logico o prefijo de device path
- secuencia ordenada sin archivos de video intercalados
- cantidad tipica de exposiciones HDR: 3, 5, 7, 9

### Regla base recomendada

Separar primero el catalogo en cuatro corrientes:

- videos
- JPG
- DNG
- otros

Luego, para JPG y DNG por separado:

1. ordenar por `creationDate` y como fallback por `name`
2. recorrer secuencialmente armando ventanas temporales
3. dentro de cada ventana, intentar detectar series consecutivas
4. aceptar grupo HDR solo si supera un umbral minimo de confianza
5. si no supera el umbral, dejar los archivos como singles

## Algoritmo concreto recomendado

### Paso 1. Normalizacion

Para cada archivo derivar:

- extension normalizada
- timestamp en milisegundos
- token de nombre base
- sufijo numerico final si existe

Ejemplo de parseo util:

- `IMG_0001.JPG` -> base `IMG_`, indice `1`
- `IMG_0002.JPG` -> base `IMG_`, indice `2`
- `R0012345.DNG` -> base `R`, indice `12345`

No hace falta que el nombre siga siempre un patron fijo. Si no se puede parsear, igual se puede usar proximidad temporal.

### Paso 2. Candidatos por ventana temporal

Agrupar candidatos del mismo tipo si entre archivos consecutivos hay una distancia menor a un umbral, por ejemplo:

- JPG: 1.5 a 2.0 segundos
- DNG: 2.0 a 3.0 segundos

El umbral de DNG deberia ser mas permisivo porque el pipeline RAW suele ser mas lento.

### Paso 3. Puntaje de continuidad

Dentro de cada ventana calcular score:

- +3 si los nombres comparten prefijo parseado
- +3 si la numeracion es consecutiva
- +2 si el gap temporal entre vecinos es pequeno y consistente
- +1 si tamanos estan dentro de una banda razonable
- +1 si la cantidad total es 3, 5, 7 o 9

Clasificar como HDR si:

- hay al menos 3 imagenes
- no hay video mezclado
- score total supera un umbral definido

## Por que esta estrategia es la mejor para este caso

Porque tolera escenarios reales:

- camaras que nombran bien la secuencia
- camaras que nombran mal pero guardan con timestamps cercanos
- casos mixtos donde JPG y DNG coexisten pero no deben fusionarse en un solo tile
- archivos sueltos que quedan fuera de cualquier set

## Regla importante: no mezclar JPG y DNG en un mismo tile

Tu requerimiento dice claramente que queres:

- una foto por cada HDR en JPG
- una foto por cada HDR en DNG

Por eso el agrupador debe operar por familia de formato.

Si una misma escena genero:

- 5 JPG
- 5 DNG

el browser debe mostrar 2 tiles:

- HDR JPG x5
- HDR DNG x5

## Thumbnail representativo

La mejor opcion es la toma del medio del set:

- 3 fotos -> indice 1
- 5 fotos -> indice 2
- 7 fotos -> indice 3

Ventajas:

- suele ser la exposicion neutra
- evita extremos sub/sobreexpuestos
- da una vista mas estable para el browser

## Comportamiento de UI recomendado

Cada tile debe mostrar:

- thumbnail del `representative`
- badge de tipo: `HDR JPG`, `HDR DNG`, `JPG`, `DNG`, `VIDEO`
- contador, por ejemplo `5 imgs`
- nombre resumido del grupo

Si el item es single:

- se comporta igual que hoy

Si el item es HDR:

- clic selecciona el grupo entero
- preview muestra la representativa
- metadata muestra resumen del set, no de un archivo aislado

## Descarga y borrado

Ambas acciones deben resolver siempre el grupo completo a archivos reales.

Eso significa:

- la seleccion visible es por `MediaAssetGroup`
- antes de llamar a `ThetaCameraService`, la app aplana `group.files`

Ejemplo:

```cpp
QList<CameraFileInfo> filesToDelete;
for (const MediaAssetGroup& group : selectedGroups) {
    filesToDelete.append(group.files);
}
```

## Casos borde que conviene soportar

- secuencias de 2 imagenes: no tratarlas como HDR por defecto
- archivos con fecha invalida: fallback a nombre
- nombres no parseables: usar proximidad temporal y tamano
- grupos ambiguos: degradar a singles
- thumbnails faltantes: pedir solo la representativa del grupo

## Cambio estructural recomendado

### No recomendado

- mantener tiles por archivo y "adivinar" set solo al descargar
- borrar multiples archivos ocultos sin que la UI lo represente
- mezclar JPG y DNG del mismo disparo en un unico tile

### Recomendado

- introducir `MediaAssetGroup`
- agrupar una sola vez al refrescar catalogo
- usar grupos en toda la capa de presentacion
- resolver a archivos reales solo en el ultimo paso de descarga/borrado
