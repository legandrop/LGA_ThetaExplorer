# Windows

La UI y los servicios son los mismos que en macOS. El acceso USB usa Windows Portable
Devices (WPD), separado del bridge ImageCaptureCore de macOS. La cámara debe estar
encendida, conectada por USB y reconocida por Windows como dispositivo portátil.

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

Delete valida toda la selección antes de enviar un borrado sin recursión. Sólo los
resultados confirmados como exitosos desaparecen del catálogo; las fallas parciales se
informan. Una selección desactualizada se rechaza.

El cierre solicita cancelar y terminar únicamente el auxiliar propio. Al destruir el
bridge, espera como máximo 1,5 segundos para limpiar la carpeta temporal. Si el proceso
continúa bloqueado en el sistema, se registra la ruta propia y puede quedar un residuo;
no se destruye un QProcess vivo ni se barre el destino del usuario por patrones.

## Previews y validación

Los JPG y DNG se leen mediante WPD. El parser portable recupera EXIF y el JPEG embebido
cuando cabe completo en el prefijo de 1 MiB; un DNG sin ese preview puede seguir mostrando
el marcador RAW. No se revelan los datos RAW ni se fusionan exposiciones HDR.

El workflow manual `Windows build` compila Debug y Release con el script del repo,
comprueba browse/stop del auxiliar sin una cámara física y arranca QApplication con el
plugin offscreen. La prueba usa sólo el runtime desplegado, sin DLL del SDK en PATH.
Usa un runtime de herramientas
propio y paquetes fijados; no publica instaladores ni releases. La prueba de USB con una
THETA real en Windows es una validación adicional: incluye conexión tardía, thumbnails,
brackets, fecha/hora, descarga, borrado con confirmación y reconexión.
