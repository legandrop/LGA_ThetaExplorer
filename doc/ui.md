# Interfaz

Como esta armada la UI desde v0.994, cuando se alineo con el resto de las apps LGA.

## Paleta y estilos

- La paleta vive en `include/thetaexplorer/ColorUtils.h`, con los nombres de `COLOR_VARS` de
  `LGA_Base_QT_C_Py` (`bg_principal`, `bg_items`, `bg_tabs`, `txt_principal`, `txt_secundario`,
  `violeta_oscuro`, `violeta_claro`...) mas los de estado (`color_success`, `color_warning`,
  `color_error`).
- `resources/styles/dark_theme.qss` usa esos nombres como variables; `ColorUtils::loadStyleSheet()`
  los reemplaza (claves largas primero) y `main.cpp` aplica el resultado a la app.
- Los widgets se estilan por `objectName` y propiedades dinamicas (`state`, `kind`, `variant`,
  `low`). No hay `setStyleSheet` con colores sueltos en los widgets; lo que se pinta por codigo
  (fondo de los tiles, glyphs) toma sus colores de constantes `k*` al principio del `.cpp`.
- Texto secundario: `#8f8f8f` como minimo, nunca los `#333`/`#444` de antes (contraste 1.5:1 a
  1.9:1 sobre los fondos oscuros).

## Fuente

Inter embebida en `resources/fonts` (Regular, Medium, Bold). El QSS y `main.cpp` declaran **una
sola familia**: con una lista de familias Qt carga todas las fuentes del sistema al arrancar (hub
Qt/C++ del Base, "font-family con lista"). Si Inter no queda registrada, `main.cpp` lo avisa en el
log.

## Iconos

Glyphs SVG monocromos en `resources/icons/ui`, dibujados en negro con margen dentro del
`viewBox`. `UiIcons::pixmap()` los rasteriza con supersampling y rect logico explicito y los tine
con `CompositionMode_SourceIn` (hub Qt/C++, "SVG chico tenido sin recorte en QLabel");
`UiIcons::buttonIcon()` arma el `QIcon` con color normal y deshabilitado. Nada de emojis ni
simbolos Unicode como iconos: macOS pinta los emojis en color.

Hace falta `Qt6::Svg`. El Help usa el signo de pregunta del Base/Player, sin circulo:
gris normal, blanco con hover y violeta mientras su dialogo esta abierto.
`compilar.sh` re-deploya si el bundle no trae `QtSvg.framework`, `QtNetwork.framework`
o el plugin TLS `libqsecuretransportbackend.dylib`.

## Ventana principal

- **Controles arriba del Viewer**, en tres filas: camara (`StatusDot`: punto verde conectada,
  arco girando buscando), bateria (roja con20% o menos) y Refresh juntos; Help queda
  en el extremo derecho. Refresh y Help usan glyphs lineales de18px en boton30px,
  gris secundario normal, gris principal al pasar el mouse y violeta para Help abierto.
  El destino Save to incluye Browse, ruta recortada por el principio y un fondo con
  borde y hover en toda la fila. Delete N neutral y Download N violeta muestran texto
  y la misma cantidad de capturas seleccionadas, sin iconos. Siguen los botones del
  Player de MediaTools. El progreso ocupa una fila
  adicional mientras descarga; no hay una barra global que quite altura a Browse.
- **Grilla**: un `QStackedWidget` alterna la grilla y `EmptyStateWidget`. Sin camara: «Connect your
  RICOH THETA» con la fila «Searching for camera…». Camara conectada sin archivos: «The camera is
  empty».
  Mientras lee el catalogo y EXIF de brackets muestra «Loading camera files».
- **Tarjetas**: 180 × 160 px por captura, miniatura168×100px y pie de dos lineas.
  Nombre14px, fecha13px, formato sobre imagen12px; Downloaded/Partial12px. Cada tarjeta
  conserva nombre y fecha/hora con mes abreviado. Sobre la imagen aparece el tipo
  y, para HDR, el formato y la cantidad de brackets: «HDRI DNG + JPG · 9».
- **Misma captura HDR**: si los sets DNG y JPG contienen las mismas tomas, se fusionan
  en una unica tarjeta neutra, sin colores alternados ni fragmentos entre filas.
  Seleccionar, descargar o borrar esa tarjeta incluye todos los archivos de ambos
  formatos. La vista previa usa el JPG central; File Info muestra HDRI, ambos
  formatos, brackets y el tamaño total. N brackets representan 2N archivos.
  Si solo queda un formato, se muestra como HDRI DNG o HDRI JPG independiente.
  La deteccion compara todas las bases de nombre, carpeta y fechas por toma
  (diferencia maxima de cinco minutos entre formatos); exige correspondencia unica
  en ambos sentidos. Si las listas difieren, son ambiguas o no tienen fechas
  fiables, se mantienen separadas. Se fusionan grupos con las mismas tomas disponibles,
  incluidos faltantes iguales. Un hueco intermedio puede subdividir la secuencia original
  durante la agrupacion por formato; la fusion no reconstruye los tramos separados.
  El bracket nativo tambien puede demorar entre exposiciones: se identifica por EXIF
  ExposureMode2 y exposiciones que contemplan tiempo, ISO y apertura, en cualquier orden,
  con nombres consecutivos, carpeta comun y hasta cinco minutos entre tomas. Una
  repeticion del patron inicial o una pausa respecto a su cadencia separa capturas;
  el tiempo de obturador evita cortar exposiciones largas. No se agrupan fotos solamente
  por compartir minuto o por parecerse. Por ejemplo, ocho brackets DNG/JPG son una
  tarjeta «HDRI DNG + JPG · 8» que incluye dieciseis archivos.
  Cuando falla una miniatura RAW de la camara, se intenta recuperar su preview
  embebido con una lectura de cabecera; no se descarga automaticamente el RAW completo.
- **Viewer**: original arriba y Stitched preview abajo, con areas iguales y proporcion
  conservada. Para DualFisheye muestra Dual lens, bracket representativo y Approximate.
  File Info compacto conserva nombre, tipo/formato/brackets, tamaño total, fecha/hora,
  path y dimensiones cuando se conoce el original. Ya no hay divisor vertical interno
  ni se restaura su antigua preferencia; el divisor Browse/Viewer sigue persistido.
  Una fila adicional muestra Last downloaded con fecha/hora local y, si es parcial,
  archivos registrados sobre el total; sin registros indica Download history: Not recorded.
  Las marcas Downloaded/Partial usan el historial confirmado, separado de la presencia
  de archivos en la carpeta de destino. No se aumenta la altura de las tarjetas.
- **Teclado**: clicar una tarjeta enfoca la grilla. Izquierda/derecha recorren los items;
  arriba/abajo conservan la columna, o llegan al ultimo item si la ultima fila queda corta.
  El scroll solo revela la nueva seleccion. Shift + flechas extiende/reduce el rango desde su
  ancla; Cmd + click alterna items, Shift + click selecciona un rango, Cmd + A selecciona todo
  y Esc limpia la seleccion.
- **Divisor Browse/Viewer**: con un catalogo cargado, al arrastrar y al cambiar el tamano de la
  ventana, Browse ajusta su ancho a la cantidad entera de columnas mas cercana que deje lugar
  para el Viewer. El handle tiene 5 px para agarrarlo; los paneles no colapsan. Separacion entre
  tarjetas: 8 px; margen izquierdo: 8 px; derecho: 4 px. El scrollbar reserva siempre sus 8 px
  para que la cantidad de columnas no oscile al aparecer o desaparecer el scroll.
- **Barra de estado**: `#101010`, texto 12 px en `txt_secundario` o en el color de estado.

## Preview panoramico

La seleccion de una imagen JPEG copia solamente su representante a la cache propia
de esa camara, sin borrar ni alterar la camara o la carpeta de descargas. La copia
comparte la cola serial de Download, con prioridad para las descargas elegidas por el
usuario. Navegar rapido conserva solo el ultimo preview pendiente; las miniaturas esperan
mientras hay una transferencia. Las señales y contadores de Download no incluyen previews.
Las miniaturas, los previews JPEG y los metadatos de brackets sobreviven al cierre y la
desconexion en `QStandardPaths::CacheLocation/camera-media` en macOS y `cache/camera-media`
junto al ejecutable en Windows, separados por numero de serie
y por ruta/nombre/tamaño/fecha UTC/UID de archivo. Solamente se copian los previews solicitados;
las panoramicas se vuelven a generar desde ese JPEG local. Las entradas desaparecen cuando
un catalogo completo confirma que ya no estan en la camara o tras un Delete exitoso; una lista
parcial, un error o una desconexion no limpian la cache. Cada camara conserva su propio
contenido hasta volver a consultar su catalogo. Sin identidad fiable o si no se puede escribir
la cache, el preview puede usar el destino temporal de la sesion. Los errores se muestran
brevemente y quedan en el log; volver a seleccionar permite reintentar.

`DualFisheyePreview` identifica ImageType en los metadatos EXIF/RICOH:1 es panoramico y se
muestra sin reproyectar;2 corresponde a lentes girados;3 a lentes ya orientados. Una
proyeccion desconocida, RAW solo o video no inventa un panorama. No se decide por la
proporcion2:1, porque el dual fisheye original de la Z1 tambien tiene esa proporcion.

El decode se limita a1600px y la reproyeccion usa un modelo equidistante de190grados con
muestreo bilineal, sin dependencias nuevas. Se ejecuta en un worker con imagenes por valor;
solo el hilo de UI crea QPixmap. Un resultado tardio no cambia otra seleccion ni sobrevive
a clear/multi/cierre; las miniaturas tardias conservan el original de mayor resolucion.
El panel conserva ocho resultados. Es un preview aproximado con costuras visibles:
no aplica calibracion optica por unidad, nivelacion IMU ni mezcla de las exposiciones HDR.

## Carteles

`ConfirmDialog` (`src/ui/ConfirmDialog.cpp`) copia el chrome de los dialogos del Player de
MediaTools (Replace track, Import Shot Review): fondo `bg_principal`, icono de 44 px, titulo 15 px
en negrita, subtitulo gris, fila de atajos con icono de teclado y teclas en violeta, y botones del
mismo ancho sin borde (neutral `#2a2a2a`, accion `violeta_oscuro`, borrar en rojo apagado). **Sin
contorno de foco**: que tecla hace que lo dicen el texto del boton («Cancel (esc)», «Skip Existing
(enter)») y la fila de atajos. En macOS los botones piden `Qt::StrongFocus` para que Tab los
recorra.

No se usa `QMessageBox`.

## Avisos flotantes

`MessagePopover` (`include/thetaexplorer/popover/`, portado de `LGA_FileManagerS3`, ver
`../LGA_Base_QT_C_Py/docs/Doc_MessagePopover.md`). `MainWindow::notifySuccess()` lo muestra 4 s;
`notifyError()` lo deja hasta que se cierra, y un error nuevo reemplaza al que este abierto. Las
descargas avisan una sola vez por lote.

## Help

`HelpDialog`, modal, se abre con el boton «?», con **Cmd+?** (`QKeySequence::HelpContents`) y con
«About ThetaExplorer» del menu de la app (`QAction::AboutRole`). Se cierra con Esc o con el
boton de la ventana, sin boton Close, como el Help del Player.

Contenido: nombre en violeta de marca `rgb(127, 98, 170)` con la version de
`QApplication::applicationVersion()`, «Developed by Lega Pugliese», link a **lega.com.ar** (violeta
apagado `#8676a3`, `#a796c6` subrayado al pasar el mouse), atajos y rutas clicables (descargas,
logs, settings) que abren la carpeta en Finder. Los colores son constantes `kHelp*` al principio del
`.cpp`. Si se agrega un atajo, se lista ahi en la misma pasada.

Tambien incluye **Check for updates automatically**, activado inicialmente y persistido en
`Updates/AutomaticChecks` del mismo INI, y **Check now**, deshabilitado con texto Checking
mientras consulta. Los resultados manuales aparecen por encima del Help; una version nueva
cierra el Help antes de presentar la oferta de actualizacion.
No se muestra la lista de teclas del dialogo de actualizacion dentro del Help.

## Actualizaciones

`libs/lgaupdate` comparte el cliente de FrameRev. Consulta el manifiesto publico de
`LGA_Updates`, para `legandrop/LGA_ThetaExplorer`, y elige el ZIP de macOS de la version
publicada. Nunca ofrece una version igual o anterior. Las copias dentro de build/deploy o del
arbol de fuentes no se reemplazan; Check now explica que la actualizacion corresponde a una
copia instalada.

La busqueda automatica comienza a los cinco segundos y se repite aproximadamente cada tres
horas, con variacion de quince minutos; tras un fallo reintenta a los diez minutos. La
comprobacion periodica del reloj permite retomar luego de reposo. El usuario debe confirmar
**Update now** antes de descargar e instalar: **Remind me later** aplaza un dia, **Skip this
version** omite esa version y **Cancel** silencia las ofertas durante la sesion. Check now
permite buscar manualmente aunque haya una version omitida o un recordatorio pendiente.
Atajos del dialogo: U actualizar, R recordar, S omitir y C/Esc cancelar.

El ZIP requiere un SHA-256 del manifiesto y se verifica antes de instalar. Descargas
interrumpidas pueden reanudarse; Cancel detiene la descarga. El cache esta en
`~/Library/Caches/LGA/ThetaExplorer/updates/`, fuera del bundle. Antes de descargar y de
instalar se comprueba que no haya descargas/borrados de camara, otra instancia o un helper
de instalacion activo. No hay limpieza del cache al arrancar la app.

El helper de macOS espera el cierre normal, valida el bundle extraido, conserva una copia de
respaldo y restaura ante un fallo de reemplazo; relanza la app al terminar. Si otra instancia
no se cierra, aborta sin forzarla ni abrir otra copia. Los fallos de consulta, descarga o
preparacion de la instalacion muestran un cartel; incluye Details cuando hay detalle tecnico
disponible. Despues del cierre, un fallo del reemplazo se informa por notificacion de macOS
y en `~/Library/Logs/LGA/LGA_ThetaExplorer/Update.log`. Las notas de una version
pueden mostrarse si el manifiesto las incluye; su ausencia no impide actualizar.

## Pendiente

- El selector de carpeta usa el dialogo de Qt (`DontUseNativeDialog`) y no esta documentado por
  que. Antes de pasarlo al nativo hay que probar que no choque con la sesion de ImageCaptureCore.
- No hay tooltips: si hacen falta, portar `CustomTooltip` del Base
  (`../LGA_Base_QT_C_Py/docs/Doc_CustomTooltip.md`), nunca `setToolTip` nativo.
