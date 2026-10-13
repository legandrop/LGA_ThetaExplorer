# Session and Download Behavior

## Persistencia

La app ahora guarda entre sesiones:

- geometria de ventana
- estado del splitter principal
- carpeta de descarga seleccionada

Se guardan en un INI: `~/Library/Application Support/LGA/ThetaExplorer/settings.ini`
(`AppPaths::settingsFile()`), igual que el `AppSettings` de las apps LGA. Hasta v0.993 iban al
plist `~/Library/Preferences/com.lga.ThetaExplorer.plist` (`QSettings` nativo): la primera vez
que arranca sin `settings.ini`, `AppPaths::migrateLegacySettings()` copia las claves `window/*` y
`downloads/*` del plist. El plist no se borra.

## Icono

El icono de la app es el del bundle (`Assets.car` + `AppIcon.icns`); en macOS no se setea en
runtime. La toolbar ya no lleva logo: arranca con el estado de la camara.
Desde1.009 los controles estan arriba del Viewer. El antiguo splitter derecho
no se restaura ni se guarda; File Info ocupa un pie compacto bajo los previews.

## Browser y estado de descarga

Cuando llega el catalogo de la camara, la app:

1. agrupa el catalogo en `MediaAssetGroup`
2. calcula las carpetas esperadas por formato; una captura DNG + JPG ocupa una tarjeta y dos carpetas
3. consulta el historial por camara y por archivo, separado de la presencia local
4. marca el tile con **Downloaded** (check) si todos sus archivos tienen una descarga
   confirmada, o **Partial** (medio circulo) si solo una parte tiene registro

El historial vive en `download-history.json`, junto a `settings.ini`: en macOS,
`~/Library/Application Support/LGA/ThetaExplorer/`; en Windows,
`%APPDATA%\LGA\ThetaExplorer`. No depende de la carpeta de destino y sobrevive a
borrarla, moverla o elegir otra. Es propio de esta computadora; no viaja con la
camara ni se borra automaticamente al formatearla.

Cada registro corresponde a la identidad estable de la camara (serial resumido con
SHA256), carpeta, nombre, tamaño, fecha de captura y UID disponible. Sin serial o
sin fecha/UID fiables no se inventa una identidad. La fecha de descarga se guarda
en UTC; File Info muestra Last downloaded en hora local y, para un grupo parcial,
cuantos archivos tienen registro sobre el total. Es la ultima transferencia registrada
de cualquier archivo del grupo, no la fecha de captura ni una garantia de copia local.

Solo se registra una descarga elegida por el usuario, una vez que el servicio verifica
el archivo recibido contra nombre, ruta de destino y tamaño esperados. Previews,
miniaturas y cabeceras quedan fuera. Los archivos existentes en disco no crean registros,
porque no prueban cuando ni con que camara se descargaron. El historial empieza con
las descargas hechas desde1.019; no reconstruye descargas anteriores ya eliminadas.

La escritura usa QSaveFile y un bloqueo entre instancias; cada modificacion relee
el archivo para no pisar registros de otra copia. Un JSON invalido se conserva sin
reemplazar. Si no se puede guardar, la descarga real sigue contando como completada
y se avisa que no quedo registrada; el detalle tecnico va al log.

## Descargar sobre algo que ya existe

Si algun item seleccionado ya tiene algo en la carpeta de descarga, se pregunta antes de bajar:

Este control examina el destino actual. Una marca historica Downloaded o Partial
no saltea archivos ni impide volver a descargarlos a una carpeta vacia.

- **Skip Existing** (Enter, la opcion por defecto): se saltean los archivos que ya estan en disco
  con el mismo tamano y se baja el resto, asi que un set a medio bajar se completa. Hasta v0.993
  este boton calculaba la lista pero encolaba todos los grupos igual.
- **Replace**: borra las subcarpetas locales del item y las vuelve a bajar. Solo con click (no entra
  en el Tab).
- **Cancel** (Esc).

## Borrar de la camara

El cartel dice cuantos items y cuantos archivos se borran en total (un set HDR son varios
archivos; 9 brackets DNG + JPG son 18 archivos). Enter y Esc cancelan; borrar requiere el click
en **Delete**, que tampoco entra en el Tab. Al terminar se saca lo borrado del catalogo sin esperar a que la camara lo vuelva a
enumerar; si no queda nada, aparece «The camera is empty».

## Desconexion

Si la camara se desconecta con una descarga en curso, se resetea el estado (progreso y botones).
Si se desconecta con el cartel de archivos existentes abierto, no se borra ni se baja nada.

## Avisos

Ademas de la barra de estado, los errores y los finales de lote muestran un aviso flotante
(`MessagePopover`, portado de FileManagerS3): el de exito se cierra solo a los 4 s, el de error
queda hasta que se lo cierra. Una descarga muestra un solo aviso al terminar el lote, no uno por
archivo.

## Estructura de descarga

Una captura DNG + JPG se selecciona y descarga con una sola accion. Sus archivos se guardan
en las dos subcarpetas por formato que la app ya usaba; no se renombran ni se mueven descargas
existentes. Downloaded exige registros de todos los archivos de ambos formatos;
Partial indica que solo una parte tiene descarga confirmada.
La fecha de cada carpeta corresponde al representativo de ese formato, incluso si sus fechas
caen a ambos lados de medianoche. Fotos, RAW, videos y HDR de un solo formato usan una carpeta.

Formato:

- `HDRI_AAMMDD_<nombre>_jpg`
- `HDRI_AAMMDD_<nombre>_dng`
- `video_AAMMDD_<nombre>_`

Donde `AAMMDD` es la fecha de captura del representativo (año 2 digitos, mes, dia).

Ejemplos:

- `HDRI_260402_R21381-21389_jpg` (set HDR: el rango pierde los ceros de adelante)
- `HDRI_260402_R21381-21389_dng`
- `HDRI_260402_R0021390.JPG_jpg` (foto suelta: el nombre completo)
- `video_260401_R0010661.MP4_`

## Fechas en tiles

Cada tile muestra las fechas de los archivos del grupo.

Color:

- verde claro para elementos muy recientes
- degradado progresivo hasta verde oscuro dentro de una ventana de 28 horas
- gris (`#8f8f8f`) para items mas viejos

Fecha y hora van en una sola linea: `Thu 1 Oct · 17:42`.

Esto sirve para detectar rapidamente capturas recientes sin abrir metadata.
