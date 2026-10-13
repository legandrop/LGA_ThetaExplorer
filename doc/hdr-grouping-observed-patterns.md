# Patrones observados en la Z1

## Fuente

Catalogo exportado real:

- `~/Desktop/catalogo/theta-catalog-20260402-030951.json`

## Hallazgos concretos

- `294` archivos totales
- `145` JPG
- `144` DNG
- `5` videos
- `1` archivo auxiliar: `setting.txt`

## Patron HDR observado

En este catalogo la Z1 muestra sets muy claros:

- `16` corridas JPG de `9` imagenes
- `16` corridas DNG de `9` imagenes

Ejemplos:

- JPG: `R0021381.JPG` a `R0021389.JPG`
- DNG: `R0021381.DNG` a `R0021389.DNG`

- JPG: `R0021471.JPG` a `R0021479.JPG`
- DNG: `R0021471.DNG` a `R0021479.DNG`

- JPG: `R0021516.JPG` a `R0021524.JPG`
- DNG: `R0021516.DNG` a `R0021524.DNG`

## Caracteristicas del patron

- mismo prefijo: `R`
- numeracion consecutiva
- familias separadas por extension
- cada set dura aproximadamente `10` segundos
- JPG y DNG aparecen espejados por el mismo rango numerico
- entre un JPG y su DNG equivalente se observaron `1` o `2` segundos de diferencia;
  son fechas de archivo y el vinculo admite hasta cinco minutos entre formatos por toma

## Decision de implementacion

La app agrupa HDR con una regla conservadora:

- misma familia de archivo
- prefijo consistente
- cercania temporal
- numeracion casi consecutiva
- minimo `3` imagenes para considerar set HDR

Esto permite:

- detectar los sets claros de `9`
- tolerar sets incompletos
- dejar como singles los casos ambiguos

## Archivo a ignorar en el browser

- `setting.txt`

No debe mostrarse como tile visual de media.
