# Windows

La UI y los servicios son los mismos que en macOS. El acceso USB usa Windows Portable
Devices (WPD), separado del bridge ImageCaptureCore de macOS. La cámara debe estar
encendida, conectada por USB y reconocida por Windows como dispositivo portátil.

La Z1 utiliza MTP por USB, accesible con el controlador nativo de Windows; no hace falta
instalar la app de Ricoh, un plugin en la camara ni una biblioteca USB externa.
Referencias: [USB API de Ricoh](https://docs-theta-api.ricoh360.com/usb-api/),
[transferencia de archivos de la Z1](https://theta-archive.ricoh360.com/support.theta360.com/uk/manual/z1/content/pc/pc_03.html)
y [Windows Portable Devices](https://learn.microsoft.com/en-us/windows/win32/wpd_sdk/programming-guide).

Si la camara no aparece, comprobar que esta encendida, usar un cable USB de datos
y conectarla directamente a otro puerto de la PC. Debe aparecer en el Explorador como
RICOH THETA antes de poder leerla desde la app. Refresh vuelve a buscar; la conexion tardia
tambien se detecta automaticamente. No cambiar el driver a WinUSB/libusb para usar esta app.

## Datos locales

Las miniaturas y previews se guardan en `cache\camera-media` junto al ejecutable;
las descargas del actualizador en `cache\updates`. Los previews temporales de una
camara sin identidad fiable quedan dentro de `cache` y se retiran al cerrar la sesion.
La carpeta de la app debe ser escribible, como cualquier instalacion portable LGA.
Los datos chicos permanecen separados: `%APPDATA%\LGA\ThetaExplorer` para settings
e historial, `%LOCALAPPDATA%\LGA\ThetaExplorer\logs` para el log.

## Compilar y abrir

Instalar Qt 6.5.3 `mingw_64`, MinGW 13.1 y Ninja, como en el Base. Las rutas predeterminadas
son `C:\Qt\6.5.3\mingw_64` y `C:\Qt\Tools\mingw1310_64\bin`. Se pueden indicar otras
instalaciones mediante `THETA_QT_DIR` y `THETA_MINGW_BIN`.

Ejecutar `compilar.bat`: configura y compila Debug incremental en `build`, copia el runtime
y abre la app sin retener la terminal. `--release` usa `build-release`, `--no-run` omite la
ejecución, `--wait` espera a que la app cierre, y `--parallel N` elige la concurrencia.
`--force-clean` es optativo; no se necesita para una compilación normal. El script cierra
solamente el ejecutable que va a reemplazar, incluidos sus auxiliares del mismo archivo.
Si el SDK MinGW trae sólo DLL Qt Release, la app Debug utiliza ese runtime disponible,
igual que los targets importados por CMake; el código de la app conserva símbolos Debug.

Los headers WPD originales de mingw-w64 v12 se usan cuando el SDK del toolchain no los
incluye. Su procedencia, hashes y licencia están en `third_party/wpd/NOTICE.md`.

## Aislamiento y operaciones

Para preparar el instalador de la version actual, usar
`instalador_local.bat --no-run` con Inno Setup 6 instalado. El paquete Release,
setup y SHA256SUMS quedan en `deploy/windows-v<version>/`. No se publican ni se
ejecutan automaticamente. Flujo y limites de validacion: [Deploy](deploy.md).

El mismo ejecutable corre un auxiliar `--camera-worker`, sin ventana, ajustes ni
actualizador. COM se ejecuta en un hilo MTA; el controlador puede cancelar desde otra
interfaz obtenida por GIT. La comunicación usa JSON UTF-8 con un pedido activo por vez,
identidad de proceso/sesión, IDs exactos y tamaños acotados. Refresh vuelve a iniciar la
búsqueda si el auxiliar se detuvo; no reenvía descargas ni borrados anteriores.

El catálogo conserva el identificador persistente de cada objeto, nombre, ruta lógica,
tamaño y fecha. Se rechazan rutas ambiguas incluso entre carpetas y almacenamientos.
Antes de descargar o borrar se vuelve a comprobar la identidad original en la cámara.
Delete rechaza un objeto sin fecha ni identidad persistente fiable; la descarga conserva
la revalidación de objeto/carpeta/nombre/tamaño en la sesión actual.

Cada descarga tiene su propia carpeta temporal dentro del destino. Se verifican todos
los bytes y se reemplaza el archivo final con `MoveFileExW` sólo cuando la copia está
completa. Los originales de la cámara no se modifican. Un timeout después del reemplazo
puede dejar un resultado incierto: se informa y no se reintenta automáticamente.
El traslado Win32 final usa rutas extendidas tanto para discos locales como UNC, incluidas
las rutas de staging y cache de mas de 260 caracteres.

Delete valida toda la selección antes de enviar un borrado sin recursión. Sólo los
resultados confirmados como exitosos desaparecen del catálogo; las fallas parciales se
informan. Una selección desactualizada se rechaza.

Si la Z1 enumera un objeto cuyas propiedades no se pueden leer, se muestran los
archivos legibles y un aviso con la cantidad de objetos omitidos. Ese catálogo se
marca parcial: no poda la cache ni verifica borrados por archivos ausentes.
Un error de enumeracion, rutas duplicadas o una estructura invalida sigue rechazando
la lista; tampoco se reemplaza un catalogo conocido por una lista parcial vacia.

El cierre solicita cancelar y terminar únicamente el auxiliar propio. Al destruir el
bridge, espera como máximo 1,5 segundos para limpiar la carpeta temporal. Si el proceso
continúa bloqueado en el sistema, se registra la ruta propia y puede quedar un residuo;
no se destruye un QProcess vivo ni se barre el destino del usuario por patrones.

## Previews y validación

Los JPG y DNG se leen mediante WPD. El parser portable recupera EXIF y el JPEG embebido
cuando cabe completo en el prefijo de 1 MiB. Si el TIFF es valido pero su preview
queda fuera del prefijo, se aprovecha la miniatura nativa WPD para conservar tambien
los metadatos disponibles. Sin ninguno de esos previews se mantiene el marcador RAW.
No se revelan los datos RAW ni se fusionan exposiciones HDR.

El workflow manual `Windows build` compila Debug y Release con el script del repo,
comprueba browse/stop del auxiliar sin una cámara física y arranca QApplication con el
plugin offscreen. La prueba usa sólo el runtime desplegado, sin DLL del SDK en PATH.
Usa un runtime de herramientas
propio y paquetes fijados; no publica instaladores ni releases.

La validacion local con una Z1 real en Windows incluye los 636 archivos legibles del
Explorador, 52 grupos despues de leer EXIF, miniaturas JPG/DNG, preview RAW, copia de ambos
formatos, historial y Refresh. La camara tambien enumera un objeto sin propiedades
legibles; se informa como lista parcial. Tres DNG no ofrecen miniatura nativa ni un
JPEG embebido dentro del prefijo y conservan el marcador RAW.
Las horas de captura mostradas coinciden con EXIF en las muestras copiadas (hasta tres
segundos de diferencia al escribir el DNG); VT_DATE no aporta una zona horaria propia.
El borrado fisico y una reconexion manual durante transferencia no estan cubiertos por
esta prueba local.
