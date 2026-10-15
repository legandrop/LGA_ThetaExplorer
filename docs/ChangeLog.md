# ChangeLog — LGA Theta Explorer

v1.027:
- Auto-update: se re-copia `libs/lgaupdate`. La pieza canonica agrega `progressDialogStyleSheet` (opcional, vacia por default: la barra de descarga con la hoja que pase la app, para que FileManagerS3 y PipeSync conserven la suya), el aviso "Update Postponed" ya no lleva la ruta del log en Details (es un "ahora no", no un fallo), y el `Update.log` anota la opcion elegida en el cartel y por que el chequeo automatico no lo mostro (salteada, pospuesta o cerrada con Cancel). Theta no fija hoja para la barra. Compila en Mac.
- [ Updates - lgaupdate rev 2, ajustes ]

- Auto-update: `libs/lgaupdate` se re-copia de la rev 2 de la pieza canonica de `LGA_Base_QT_C_Py`, que suma como opciones lo que tenia el updater de FileManagerS3 y PipeSync (log de updates, cierre forzado, procesos de fondo, notas por edicion). Theta no activa ninguna; cambia para todas las apps: el helper de macOS deja siempre su log en `~/Library/Logs/LGA/<productId>/Update.log` con el paso y el comando que fallo, el de Windows reintenta de verdad si no puede lanzar el instalador, y un manifiesto de formato mas nuevo pide reinstalar en el chequeo manual. Compila en Mac.
- [ Updates - lgaupdate rev 2 ]

- Mac - Lo que faltaba decir de la publicacion con `publish_release.sh`: `--dry-run` ya no solo avisa, corta igual que una publicacion real (arbol limpio, el bump hecho y, si el tag es nuevo, HEAD en la punta de `origin/main`); se quita el override `LGA_RELEASE_REPO`, porque con otro repo la pieza lo trata como repo de release aparte; y `--yes` queda aceptado pero sin efecto en esta app (solo saltea el control del What's new, que Theta no usa), como lo dicen ahora la ayuda y `doc/deploy.md`.
- [ Deploy Mac - Aclarar --dry-run, --yes y LGA_RELEASE_REPO ]

- Mac - La publicacion de `deploy.sh` pasa a `tools/macos/publish_release.sh`, copia identica de la pieza canonica de `LGA_Base_QT_C_Py`, con lo propio de esta app en `tools/macos/release.conf` (version de `CMakeLists.txt`, `bump_version.sh --check`, titulo y notas del release; sin What's new ni `SHA256SUMS` de macOS, para no romper la subida de Windows). Se llama antes de compilar y otra vez con el ZIP y el DMG armados. Cambios de comportamiento: publicar implica el ZIP y el DMG y se pregunta primero; si el tag ya existe tiene que apuntar a HEAD; un release que ya tiene los archivos de macOS pide `--replace` (antes los reemplazaba); `--dry-run`, `--replace` y `--yes` van solo con `--publish`; no hay mas confirmacion final despues de compilar (`--publish` publica), y si la publicacion falla la app se instala igual y el script sale con error.
- [ Deploy Mac - Publicar con la pieza comun publish_release.sh ]

- Auto-update (Mac): si el helper posterga el update porque sigue abierta otra ventana de Theta Explorer, la notificacion ahora dice que se postergo y pide cerrarlas; antes decia que la app "was restored". Viene de la `libs/lgaupdate` canonica, que ademas suma `relaunchWhenUpdatePostponed` (Theta no lo usa: no reabre para no sumar una ventana).
- [ Updates - Aviso de update postergado en Mac ]

- Auto-update: `libs/lgaupdate` pasa a ser copia identica de la pieza canonica de `../LGA_Base_QT_C_Py` (rev 1), que nacio de esta copia y ahora usan tambien FrameRev, Media Tools y Video Downloader. Ya no incluye `thetaexplorer/ConfirmDialog.h` ni la carpeta `include/` de la app: describe cada cartel con un `LgaUpdateDialogSpec` y `MainWindow::setupUpdates()` lo traduce a `ConfirmDialog` en `presentDialog`. El servicio decide por el rol del boton, no por su posicion. Sin cambios visibles. Verificado en Mac: compila; el update de punta a punta se probo con Video Downloader, que usa la misma copia.
- [ Updates - lgaupdate como copia de la pieza canonica ]

- `deploy.sh` (macOS) termina con la app instalada en `/Applications` y abierta, también cuando publica: el `.app` de `deploy/` no se ejecuta nunca, porque es el que se empaqueta y ejecutarlo le rompe la firma que verifica el auto-update. Lo hacen `tools/macos/install_to_applications.sh` y `tools/macos/verify_bundle_signature.sh`, copias idénticas de las piezas canónicas de `LGA_Base_QT_C_Py`; la firma se verifica después de firmar, antes del ZIP y antes del DMG. Flags `--no-install` y `--no-open`; la pregunta de Finder sigue.
- [ Deploy Mac - Instalar en Applications y abrir al terminar ]

- `deploy.sh` e `instalador.bat` preguntan siempre si publicar en GitHub y, si falta el bump, ofrecen correrlo en vez de armar solo lo local sin avisar.
- [ Deploy - Preguntar siempre si publicar ]

- Cuando el bump de version quedo hecho pero sin commitear, `bump_version --check` explica que hay que commitearlo o descartarlo con `git checkout` antes de volver a correr el bump.
- [ Versionado - Mensaje del check con bump sin commitear ]

- `instalador.bat` suma el instalador y `SHA256SUMS` al release que ya creo la Mac para esa version, desde el mismo commit, en vez de rechazarlo; sigue sin reemplazar un instalador de Windows ya publicado.
- [ Windows - Instalador suma su setup al release de la Mac ]

- La version pasa a subir solo al publicar: el changelog junta las entradas bajo la version en desarrollo, y `bump_version.sh` / `bump_version.bat`, uno solo para Windows y macOS, la lleva a `CMakeLists.txt`, la commitea y ofrece publicar; `deploy.sh` e `instalador.bat` no publican sin ese bump.
- [ Versionado - Bump manual al publicar ]

v1.026:
- Las preguntas Y/N del instalador se muestran completas antes de esperar la respuesta, usando choice directamente en la consola como los otros instaladores. Se elimina la captura de salida que retenia el texto hasta pulsar una tecla y se conserva el flujo de instalar, revelar en Explorer y publicar.
- [ Windows - Preguntas del instalador visibles ]

v1.026:
- Los avisos de error se cierran automaticamente a los cuatro segundos y muestran la barra inferior que se consume, como los avisos de PipeSync. La cuenta se pausa al pasar el mouse por encima y conserva la cruz para cerrar antes; el detalle permanece en la barra de estado y en el log.
- [ UI - Avisos con barra y cierre automatico ]

v1.026:
- El aviso de una entrada de camara ilegible explica que Windows no pudo identificar el item, cuantos archivos legibles siguen disponibles y que no hace falta actuar. Ya no pide Refresh durante una carga normal. La pantalla de carga indica que prepara grupos HDR y pide esperar mientras avanza el contador.
- [ Windows - Aviso de camara y carga mas claros ]

v1.026:
- El instalador conserva las preguntas Y/N del resto de las apps y ofrece publicar en GitHub al final sin requerir flags, despues de las ofertas independientes de instalar y revelar el archivo. El modo local y las corridas sin interaccion no publican.
- La primera lectura de metadatos muestra cuantos archivos se encontraron y cuantas fotos se procesaron. Refresh permite reiniciar esa lectura sin esperar a que termine, invalidando respuestas viejas; sigue bloqueado durante descargas, recuperacion de transferencias y borrados. El aviso de catalogo parcial ya no afirma que la grilla esta visible mientras se leen los metadatos.
- [ Windows - Instalador YN y progreso con Refresh ]

v1.026:
- Se prepara el primer instalador de Windows conservando la version 1.026: instalador.bat genera Release, empaqueta solo el runtime necesario y produce el setup con SHA256SUMS. instalador_local.bat permite armarlo sin GitHub; --publish verifica identidad, commit remoto y version sin publicar antes de la confirmacion final.
- El setup instala en C:\Portable\LGA\ThetaExplorer, crea accesos directos y cierra las copias de la app con su auxiliar. La desinstalacion retira la cache propia y conserva ajustes, historial y fotos descargadas. La version se mantiene por decision expresa para el primer lanzamiento.
- [ Windows - Primer instalador 1.026 ]

v1.026:
- El traslado final de una descarga en Windows usa rutas Win32 extendidas, tambien para destinos UNC. Corrige el preview JPG y las descargas cuyo staging o destino supera 260 caracteres; la verificacion de tamaño e identidad y el reemplazo atomico se conservan.
- [ Windows - Descargas y previews con rutas largas ]

v1.025:
- Windows limita las miniaturas pendientes al ritmo del canal USB serial. Las peticiones restantes esperan en el servicio, incluidas las de una seleccion, para no llenar la cola acotada del auxiliar ni perder imagenes durante un catalogo grande.
- [ Windows - Cola de miniaturas USB acotada ]

v1.024:
- El catalogo de Windows muestra los archivos legibles aunque la Z1 enumere un objeto sin propiedades disponibles. Avisa cuantos objetos faltan y marca la lista como parcial para conservar la cache y no confirmar borrados por ausencias. Rutas duplicadas, estructuras invalidas y errores de enumeracion siguen rechazando el catalogo.
- Los metadatos DNG aprovechan la miniatura nativa de WPD cuando el JPEG embebido queda fuera del prefijo de lectura de 1 MiB, sin descargar el RAW completo ni aceptar una cabecera invalida.
- [ Windows - Catalogo Z1 parcial y metadatos DNG ]

v1.023:
- En Windows, las miniaturas, previews y descargas del actualizador quedan dentro de cache junto al ejecutable; settings e historial permanecen en AppData. Los previews temporales tambien quedan dentro de la carpeta de la app y se retiran al cerrar su sesion.
- La deteccion del arbol de desarrollo en Windows usa marcadores de CMake y las fuentes del proyecto, sin confundir una instalacion cuyo nombre contiene build o deploy.
- [ Windows - Cache portable y deteccion de desarrollo ]

v1.022:
- Los callbacks diferidos de ImageCaptureCore conservan una copia propia de la identidad de cada miniatura y cabecera. Evita leer una referencia local ya destruida, que hacia descartar las miniaturas y dejaba vacia la cache de imagenes en macOS.
- [ Imagenes - Cache por camara y callbacks Mac seguros ]

v1.021:
- Las miniaturas JPG/RAW, los metadatos de brackets y los JPG usados por el Viewer se conservan en una cache persistente separada por numero de serie de camara y por identidad de archivo. Reconectar o reiniciar reutiliza las imagenes disponibles sin pedirlas otra vez; los previews no generan marcas de descarga.
- La cache retira solamente los archivos ausentes en un catalogo completo confirmado o los borrados exitosos de Delete. Una lista inicial parcial, una desconexion o un error no limpia imagenes; cambiar de camara, fecha, tamaño o identificador de archivo evita reutilizar otra toma. Las respuestas tardias de miniaturas se validan por peticion e identidad.
- Help conserva los controles de actualizacion y elimina la linea de teclas del dialogo de updates.
- [ Imagenes - Cache persistente por camara y Help limpio ]

v1.020:
- El nombre de camara, bateria y Refresh quedan juntos en un bloque compacto; Help conserva el extremo derecho. Refresh y Help usan trazos finos de18px y los grises de la paleta, sin blanco intenso. El nombre de camara baja a peso regular.
- El destino incluye Browse y un fondo con borde y hover en toda la fila para identificarlo como clickeable. Delete y Download muestran solamente texto y ambos indican la cantidad de capturas seleccionadas; sus confirmaciones y acciones conservan los archivos completos de cada captura.
- [ UI - Controles compactos y destino visible ]

v1.019:
- El historial de descargas se guarda de forma permanente junto a settings.ini, separado del destino y por identidad de camara/archivo. Registra fecha UTC solamente tras una transferencia del usuario con ruta, nombre y tamaño verificados; previews y cabeceras no cuentan. Escritura atomica y bloqueo entre instancias conservan los registros anteriores ante errores o archivos invalidos.
- Las tarjetas muestran Downloaded o Partial desde ese historial, aunque se mueva o borre la carpeta de descarga; File Info muestra la ultima fecha/hora y el progreso parcial por archivo. Skip Existing y Replace siguen revisando los archivos locales, sin saltarse nuevas descargas por una marca historica. No se inventan registros para descargas anteriores.
- [ Descargas - Historial permanente junto a settings ]

v1.018:
- Windows incorpora acceso USB mediante Windows Portable Devices en un auxiliar aislado: descubre la THETA, enumera sus archivos, lee miniaturas y metadatos, descarga con verificacion de bytes y permite borrar solo los objetos confirmados. Las operaciones conservan identidad de sesion y archivo, rechazan rutas ambiguas y no se repiten automaticamente tras un timeout.
- compilar.bat conserva Debug/Release separados, copia el runtime por rutas explicitas y abre la app sin retener la terminal. Refresh permite reiniciar la busqueda sin camara; Help muestra los atajos de cada plataforma y el actualizador identifica el ejecutable de Windows. Las miniaturas y metadatos se invalidan cuando cambia la identidad persistente; fechas desconocidas o carpetas diferentes no forman HDRI por proximidad.
- [ Windows - Camara USB y runtime ]

v1.017:
- La preparacion de Windows usa un runtime de herramientas embebido y fijado por version/SHA256, sin depender del Python global del runner. Se instala MinGW13.1 desde el paquete oficial disponible, coincidente con el toolchain del resto de las apps.
- [ Build - Runtime propio y toolchain Windows ]

v1.016:
- La verificacion de Windows usa Qt6.5.3/MinGW en un runner independiente y acepta una candidata temporal con SHA256 y rutas acotadas, sin modificar la rama ni crear releases. Se separan Debug y Release y se comprueba el cierre del auxiliar de camara sin dispositivo fisico.
- [ Build - Verificacion multiplataforma ]

v1.015:
- La aplicacion pasa a llamarse Theta Explorer: ventana, Help, bundle, ejecutable, scripts, archivos de distribucion y actualizador adoptan la nueva identidad. El repositorio publico nuevo parte de una copia limpia del codigo; el anterior queda como respaldo privado, sin trasladar sus commits ni tags.
- Se conservan el icono aprobado, la firma y el identificador del bundle de macOS. Los ajustes existentes se copian al nuevo INI cuando todavia no existe, manteniendo el archivo anterior.
- [ App - Theta Explorer ]


v1.014:
- El corte por pausa tambien protege capturas incompletas: si solo quedan una o dos fotos antes de una nueva serie, se mantienen separadas en lugar de incorporarlas al siguiente HDRI. El minimo de tres exposiciones se exige para confirmar un patron repetido y para formar un HDRI, sin impedir reconocer una pausa entre capturas.
- [ HDR - Brackets y capturas incompletas ]

v1.013:
- El bracket nativo admite ordenes de exposicion personalizados que suben y bajan, sin fragmentarlos por cada cambio de direccion. El reinicio se confirma con las tres exposiciones iniciales y una pausa se compara contra la cadencia de la propia captura, descontando el obturador para conservar exposiciones largas. La serie de hangar R0010739–R0010762 queda en dos tarjetas de doce brackets, con sus archivos y acciones completos.
- Las cabeceras conservan tambien el tiempo de exposicion, incluido el cache de Refresh y la correspondencia DNG/JPG. Se mantiene el techo de cinco minutos, los controles de nombre/carpeta/EXIF y la deteccion del plugin.
- [ HDR - Brackets con orden de exposicion personalizado ]

v1.012:
- Una lectura de cabecera vencida recupera la sesion de camara antes de retomar las operaciones USB: el timeout no cancela por si mismo la peticion de ImageCaptureCore. El catalogo recuperado conserva los metadatos validos, los previews RAW disponibles y los archivos completos. Las fotos sin datos suficientes siguen separadas y Refresh permite volver a intentar la lectura.
- La purga de previews tras Delete elimina solamente copias temporales que existian en el cache, evitando warnings por rutas vacias.
- [ HDR - Brackets nativos y previews RAW ]

v1.011:
- El exito de Delete retira tambien los archivos borrados de las colas de miniaturas, cabeceras y descargas, y de los caches de preview. Se evita reutilizar un handle fisico que ya no existe al reanudar la cola.
- El catalogo se mantiene ocupado durante toda la lectura EXIF, tambien entre cabeceras, para que Refresh no interrumpa una serie parcialmente reconocida. Una descarga pendiente espera la publicacion del catalogo y conserva prioridad sobre las miniaturas RAW pendientes.
- [ HDR - Brackets y colas de miniaturas ]

v1.010:
- Las secuencias del bracket nativo se identifican por EXIF ExposureMode, nombres consecutivos, carpeta, fechas y progresion de exposicion. Se contemplan cambios de ISO y apertura, hasta cinco minutos entre tomas y el corte al reiniciarse la escalera. Cada captura DNG/JPG conserva una tarjeta HDRI con todos sus brackets y archivos; la deteccion del plugin se mantiene.
- El catalogo lee solamente las cabeceras necesarias en una cola serial antes de mostrarse, reutiliza metadatos al refrescar y muestra Loading camera files durante la lectura. Si ImageCaptureCore no entrega una miniatura RAW, recupera el preview embebido con ImageIO leyendo como maximo un megabyte, sin copiar el archivo completo ni alterar Download. Las lecturas no se superponen con descargas o borrados, tienen timeout y descartan respuestas obsoletas.
- [ HDR - Brackets nativos y miniaturas RAW ]

v1.009:
- Los controles de camara, bateria, Refresh, Help, destino y Download/Delete pasan arriba del Viewer; Browse conserva toda la altura. Refresh copia PipeSync y las acciones adoptan la familia de botones del Player de MediaTools. File Info conserva todos sus datos en un pie compacto y se elimina el divisor interno vertical anterior. Las tarjetas por captura permanecen intactas.
- El Viewer muestra el JPEG original y un preview panoramico debajo. Lee la proyeccion EXIF/RICOH para convertir DualFisheye o conservar una imagen ya unida. Decode y reproyeccion acotados se ejecutan en segundo plano, descartan resultados de otra seleccion y mantienen un cache de ocho. El preview es aproximado, con costuras visibles, sin calibracion optica ni mezcla HDR.
- La copia temporal del representante comparte la cola de camara con las descargas, conserva solo el ultimo preview pendiente y no modifica sus contadores ni los originales. Las miniaturas esperan durante transferencias, las rutas relativas de ImageCaptureCore se resuelven contra el destino y los errores reales llegan a la interfaz. Download de una tarjeta sigue incluyendo JPG y DNG.
- [ UI - Controles arriba y preview panoramico ]

v1.008:
- Los resultados manuales de Updates se asocian al modal Help mediante su cadena de parents: isAncestorOf descartaba ese dialogo por ser una ventana propia. Help conserva el alto necesario para sus atajos y rutas envueltas, evitando recortes al incorporar Updates.
- La compilacion incremental detecta tambien la ausencia del backend TLS nativo de macOS y redeploya Qt para que las consultas HTTPS funcionen en bundles existentes.
- [ UI - Help y updates con HTTPS ]

v1.007:
- El boton Help adopta el signo de pregunta del Base y Player, sin circulo, con estados normal, hover y Help abierto. El dialogo agrega la preferencia de busqueda automatica y Check now, con estado Checking mientras consulta.
- Se incorpora el actualizador compartido de FrameRev: consulta al inicio y periodicamente, compara versiones publicadas, permite actualizar, recordar mañana, omitir una version o cancelar, y descarga el ZIP con verificacion SHA-256 obligatoria. La instalacion conserva backup y rollback, espera el cierre y relanza la app; las copias de desarrollo no se reemplazan. Descargar/borrar de la camara, otra instancia o un instalador activo bloquean la instalacion. El cache queda fuera del bundle y no se barre al arrancar.
- La compilacion incorpora QtNetwork y redeploya las dependencias cuando falta su framework en un bundle incremental. Se documentan el flujo y los atajos del actualizador.
- [ UI - Help y actualizacion automatica ]

v1.006:
- Cada captura HDR con las mismas tomas en DNG y JPG aparece como una sola tarjeta neutra: HDRI DNG + JPG, cantidad de brackets, nombre, fecha y hora. Se eliminan los contenedores dobles, colores alternados y marcas de continuidad entre filas. El catalogo cuenta una captura y la vista previa usa la toma JPG central.
- Seleccionar una captura incluye ambos formatos en Download y Delete. N brackets son 2N archivos; File Info suma el tamaño de ambos. Saved exige ambos completos y Partial refleja cualquiera incompleto. Skip Existing completa lo que falta en los dos formatos y Replace reemplaza sus dos carpetas; se conservan los destinos _jpg/_dng existentes y la fecha propia de cada formato.
- [ HDR - Una tarjeta por captura DNG y JPG ]

v1.005:
- Las versiones DNG y JPG de una captura HDR forman una tarjeta doble con un pie compartido de dos lineas, visible sin seleccionar: nombre y cantidad de brackets a la izquierda, fecha y hora a la derecha. Cada pareja alterna entre dos violetas de la paleta LGA; el cuerpo y el borde se derivan de HDR_PAIR_COLOR_A y HDR_PAIR_COLOR_B en ColorUtils.h. Si pasa de fila, conserva su tono y presenta bordes rectos en el corte. La seleccion, descarga y borrado siguen actuando por formato.
- Se compactan todas las tarjetas a180×160px con miniatura168×100px. DNG/JPG, o el tipo y cantidad de un HDR independiente, se muestran sobre la imagen; fotos, RAW y videos conservan nombre y fecha/hora en el pie. Al quitar una compañera, la restante vuelve a tarjeta independiente con toda su informacion.
- [ UI - Tarjetas HDR dobles con violetas alternados ]

v1.004:
- Se corrige el vinculo HDR DNG/JPG con la camara real: la comparacion temporal de un segundo rechazaba casi todas las parejas porque los archivos DNG y JPG equivalentes tienen fechas diferentes. Se admite hasta cinco minutos entre formatos por toma; siguen siendo obligatorios los mismos nombres de todas las tomas disponibles, misma carpeta, fechas validas y pareja unica. El glyph compartido, el borde de la compañera y «Same capture» ahora aparecen para esos sets sin cambiar la seleccion ni las acciones por formato.
- [ HDR - Vincular DNG y JPG con fechas de archivo diferentes ]

v1.003:
- Se unifican los scripts de compilacion con la convencion de las otras apps: el motor de desarrollo `compilar_dev.sh` pasa a `compilar.sh` y se elimina el compilador antiguo duplicado. El comando habitual conserva Debug incremental, binario universal, firma validada y lanzamiento en background, junto con `--release`, `--no-run`, `--wait` y los demas flags existentes. Deploy, documentacion y referencias vigentes actualizados al nombre unico.
- [ Build - Unificar compilacion en compilar.sh ]

v1.002:
- `compilar_dev.sh` firma y valida el bundle terminado antes de lanzar, tambien en builds incrementales y con `--no-run`. Usa la misma identidad estable del deploy (`LGA Code Signing`, override `LGA_CODESIGN_IDENTITY`) y firma ad-hoc si no esta disponible. Una firma o validacion fallida detiene el script. Sin firma, macOS podia negar el acceso a la camara con «Failed to get code requirements» y dejar la app buscando indefinidamente, aunque la Z1 estuviera visible por USB.
- [ Mac - Firmar builds locales para detectar la camara ]

v1.001:
- Se precisa el criterio de vinculo de v1.000: lo que debe coincidir son todas las tomas disponibles en ambos formatos. Si falta la misma exposicion en DNG y JPG, siguen siendo versiones de la misma captura; si falta solo en uno de los formatos o las tomas son diferentes, no se vinculan. Documentacion de interfaz y arquitectura actualizada con esta distincion.
- [ UI - HDR vinculados y tarjetas compactas ]

v1.000:
- Los HDR DNG y JPG de la misma captura muestran un icono de enlace y el mismo identificador corto debajo de la fecha. Al seleccionar una tarjeta, la compañera tiene un borde verde azulado, incluso en otra fila; File Info indica el otro formato disponible. La compañera no se selecciona automaticamente para descargar o borrar.
- El vinculo compara todas las bases de archivo, la carpeta y las fechas por toma (hasta un segundo de diferencia); solo se acepta una pareja unica. Los sets incompletos, ambiguos o sin fechas fiables quedan separados. Al borrar un formato, la tarjeta restante pierde el vinculo.
- Se elimina el hueco inferior excesivo: margen final de 6–7 px, tarjetas de 180 × 190 px sin pareja y 180 × 216 px con la fila de enlace. La grilla alinea las tarjetas arriba y calcula cada fila con su altura real. Version 1.000 como hito.
- [ UI - Vinculo HDR DNG-JPG y tarjetas compactas ]

v0.999:
- Browse toma foco al clicar una tarjeta y permite recorrer los items con las flechas: izquierda/derecha por item, arriba/abajo por columna, con scroll solo para revelar la seleccion. Shift + flechas extiende o reduce el rango; los atajos nuevos tambien estan en Help. Borrar una tarjeta reinicia sus referencias de seleccion y los clicks con scroll ya no deseleccionan por coordenadas incorrectas.
- Textos de las tarjetas dos pixeles mas grandes: nombre 14 px, tipo y fecha 13 px, badges Saved/Partial 12 px. Tarjetas de 180 × 216 px con thumbnail de 168 × 120 px; la grilla usa esas mismas dimensiones al medir filas y columnas.
- El divisor Browse/Viewer ajusta a columnas completas al arrastrar y al cambiar el tamano de ventana. Margen derecho reducido a 4 px, izquierdo a 8 px, scrollbar estable de 8 px y handle de 5 px. El minimo del estado vacio oculto ya no limita Browse; los paneles no colapsan. Se corrige el calculo que reservaba un gap tras la ultima columna y se liberan los wrappers del layout al reorganizarlo.
- [ UI - Flechas, textos legibles y divisor por columnas ]

v0.998:
- La app de macOS se genera e instala como `LGA ThetaMacExplorer.app`, tambien dentro del ZIP y el DMG, para quedar agrupada con las demas apps LGA en Applications. [ Mac - Prefijo LGA en la app y el empaquetado ]

v0.997:
- README: captura de la ventana principal (`doc/images/thetamacexplorer_main.jpg`) en los dos idiomas, con una sesion verosimil de una Z1: sets HDR JPG y DNG, badges Saved y Partial, un video y la vista previa del set seleccionado. Los thumbnails son HDRIs CC0 de Poly Haven, acreditados en el README. Se genero con la UI real (fuentes del repo, servicio de camara simulado) dibujada fuera de pantalla.
- Bug: con un set HDR seleccionado, «File Info» mostraba «Size (px): 5 images». La cantidad de imagenes ya esta en Type («HDR JPG · 5»); la fila pasa a mostrar la resolucion, como en un archivo suelto. Pero el bridge (`ThetaBridge.mm`) nunca carga ancho y alto: los deja en 0 para no pedir metadata durante la enumeracion. Asi que, mientras no haya dato, la fila «Size (px)» se oculta en vez de mostrar siempre un guion. Leer `ICCameraFile.width/height` queda pendiente de probar con la camara conectada.
- [ README - Captura con HDRIs + fix Size (px) de los sets ]

v0.996:
- Deploy: `deploy.sh` nuevo, de un solo paso. Pregunta al principio si armar el ZIP, el DMG y si publicar, y despues compila Release universal en `build-release/` (`compilar_dev.sh --release`, arbol separado que no toca la cache de desarrollo), copia el bundle a `deploy/`, valida que todo sea universal, firma (identidad estable `LGA Code Signing` si esta, si no ad-hoc), arma y verifica el ZIP (symlinks y firma) y el DMG, y si se confirma crea el tag `v<version>` y el release en este mismo repo, que es publico. Antes de compilar chequea lo que haria fallar la publicacion (gh, cuenta de git, working tree limpio, `main` igual a `origin/main`), y la publicacion pide una confirmacion final que por defecto es no. Esquema de LGA_FrameRev y LGA_Calmate.
- `create_dmg.sh` copiado de LGA_Calmate, con `dmgbuild` vendorizado (Python puro, no instala nada), fondo oscuro versionado en `resources/dmg/` y un `Read Me.txt` en ingles con el `sudo xattr -cr` para abrir la app sin notarizar.
- `CMakeLists.txt` deja de forzar `CMAKE_BUILD_TYPE Debug`: Debug sigue siendo el default, pero ahora se puede compilar Release.
- README nuevo en ingles (`README.md`) y en castellano (`README_ES.md`): el repo es publico y no tenia ninguno. Documentacion del deploy en `doc/deploy.md`.
- **Bug: el bundle salia sin `CFBundleIdentifier`.** `CMakeLists.txt` usaba `MACOSX_BUNDLE_IDENTIFIER`, una propiedad que CMake no conoce, asi que el `Info.plist` quedaba con el identificador vacio y la firma usaba `ThetaMacExplorer`. Pasa a `MACOSX_BUNDLE_GUI_IDENTIFIER` (`com.lga.thetamacexplorer`). Como cambia la identidad de la app, macOS puede volver a pedir el permiso de la camara una vez.
- `dmgbuild`, `ds_store` y `mac_alias` vendorizados llevan su `LICENSE` (MIT exige el aviso en cada copia), y `refresh_dmg_vendor.sh` lo copia del dist-info al regenerar.
- `doc/session-and-download-behavior.md` tenia mal el ejemplo de carpeta de un set HDR: el rango pierde los ceros (`R21381-21389`).
- [ Deploy - deploy.sh con DMG y release en GitHub + README ]

v0.995:
- Build macOS: la app pasa a binario **universal** (`arm64;x86_64`) y corre nativa en Apple Silicon. `compilar_dev.sh` y `compilar.sh` se relanzaban enteros bajo Rosetta y compilaban solo `x86_64`, asi que en una Mac M la app corria emulada y macOS avisaba que una app Intel deja de funcionar en una version futura. Qt 6.5.3 ya traia sus frameworks y plugins universales; el unico binario Intel era el de la app. Mismo criterio que `LGA_Base_QT_C_Py`: `CMakeLists.txt` pide `arm64;x86_64` en un host arm64, los scripts pasan la misma lista, y `compilar_dev.sh` reconfigura solo (sin borrar `build/`) si el cache tenia otras arquitecturas. Se copia del Base `tools/macos/validate_universal_macho.sh`, que corre al final del build y avisa si algun binario del bundle no es universal (hoy: 30 de 30). `compilar_dev.sh` suma `--force-rosetta` para lanzar la mitad Intel cuando haga falta probarla.
- [ Build - Binario universal, corre nativo en Apple Silicon ]

v0.994:
- UI: la app se alinea con el resto de las apps LGA (detalle en `doc/ui.md`).
  - **Help nuevo**: dialogo con nombre y version, «Developed by Lega Pugliese», link a lega.com.ar, atajos y rutas clicables de descargas, logs y settings. Se abre con el boton «?» de la toolbar, con Cmd+? y con «About ThetaMacExplorer» del menu de la app.
  - **Borrar ya no confirma con Enter**: el foco arrancaba en «Delete», asi que Enter borraba de la camara sin vuelta atras. Ahora Enter y Esc cancelan, y el cartel dice cuantos archivos se borran en total (un set HDR son varios).
  - **Bug: «Skip Existing» no salteaba nada.** Calculaba que bajar pero encolaba todos los grupos seleccionados igual. Ahora saltea los archivos que ya estan en disco con el mismo tamano y baja el resto (un set a medio bajar se completa), y es la opcion por defecto (Enter) del cartel. «Replace» y «Delete» quedan fuera del Tab: solo con click.
  - Desconectar la camara con una descarga en curso, o con el cartel de archivos existentes abierto, ya no deja los botones trabados ni borra carpetas locales. Los errores de camara o de borrado ya no cortan un lote de descarga, y el aviso de fin de lote sale una sola vez.
  - Al borrar se actualiza el catalogo sin esperar a que la camara lo vuelva a enumerar; si no queda nada aparece «The camera is empty».
  - Carteles nuevos (`ConfirmDialog`) con el estilo de los dialogos del Player de MediaTools, sin contorno de foco; reemplazan al `QMessageBox` y al `ConfirmDeleteDialog`.
  - Toolbar reordenada: estado de la camara y bateria a la izquierda, destino agrupado, Download con la cantidad seleccionada, Delete aparte y solo con borde, boton de Help. Iconos SVG monocromos en vez de emojis (`UiIcons`, suma `Qt6::Svg`; los scripts de compilacion re-deployan si el bundle no trae `QtSvg.framework`).
  - Estados vacios en la grilla: sin camara («Connect your RICOH THETA», buscando) y camara sin archivos.
  - Contraste: el texto secundario pasa de `#333`/`#444` (1.5:1 a 1.9:1) a `#8f8f8f` y 12 px; badges «Saved»/«Partial» con icono en vez de «DL»/«PART»; fecha y hora del tile en una linea.
  - Inter embebida (el QSS la pedia pero no estaba en el bundle) con una sola familia declarada.
  - Estilos: la paleta de `ColorUtils` toma los nombres de `COLOR_VARS` del Base y el QSS los usa como variables; se sacan los `setStyleSheet` con colores sueltos y el QSS duplicado en C++.
  - Avisos flotantes (`MessagePopover`, portado de FileManagerS3) para errores y finales de lote: un solo aviso por descarga, los errores quedan hasta que se cierran.
- Logs: pasan de `/tmp/ThetaMacExplorer.log`, que se borraba al reiniciar, a `~/Library/Logs/LGA/ThetaMacExplorer/debug.log`.
- Settings: pasan del plist (`QSettings` nativo) a `~/Library/Application Support/LGA/ThetaMacExplorer/settings.ini`; la primera vez se migran solos. Se saca el `organizationDomain` `lga.com`.
- [ UI - Help, carteles, toolbar e iconos al estilo LGA + fix Skip Existing ]

v0.993:
- Repo: el repo es publico y versionaba sus reglas internas de trabajo, con el script que las sincronizaba y su hook; salen del repo. Ademas se limpia la historia: se sacan de todos los commits esas reglas, la carpeta `build/` y los `.DS_Store` versionados al principio (con rutas de la maquina) y una ruta personal de `doc/`, y todos los commits quedan con la cuenta de GitHub correcta. Las entradas viejas de este changelog que describian esas reglas se reescribieron en la misma pasada. Cambian todos los hashes.
- [ Repo - Reglas internas fuera del repo publico e historia limpia ]

v0.992:
- Versionado: el formato pasa de **2 a 3 decimales** (`0.XXX`), y el bump-per-entrada de `+0.01` a `+0.001`. Con 2 decimales la app venia en `0.99`, asi que **la entrada siguiente —cualquiera— se llevaba puesto el `1.00`**: la de line endings de aca abajo lo hizo, y un commit de tooling no puede ser el que declare el hito. Esa entrada se renumera a `v0.991` (mismo contenido, solo el numero) y el `1.00` queda libre para cuando Lega decida que la app lo merece. Se actualiza la regla del repo y el `project(... VERSION ...)` del `CMakeLists.txt`, que es la unica fuente de verdad del numero.
- [ Repo - Versionado de 3 decimales ]

v0.991:
- Repo: el repo no tenia `.gitattributes`, asi que cada archivo quedaba con los finales de linea de la maquina que lo escribio y git no normalizaba nada. Se agrega la politica LGA —LF por defecto, CRLF solo en `.bat`/`.cmd`/`.ps1`— en `.gitattributes` mas un `.editorconfig` que hace que el editor los escriba bien antes de que git los vea. **No hizo falta renormalizar**: el indice ya estaba 100% en LF, y `git add --renormalize .` dio cero archivos, asi que no hay commit mecanico ni `.git-blame-ignore-revs`. Lo unico que estaba mal era el disco: un `.bat` tenia LF y ahora quedo en CRLF. La regla se suma ademas a las reglas del repo, porque `.gitattributes` resuelve el caso automatico pero no impide que un editor reescriba un archivo entero.
- [ Repo - Politica de line endings ]

v0.99:
- Build: `compilar_dev.sh` deja de tomar la terminal. Terminaba con `exec "$APP_BIN"`, que reemplaza el proceso del shell con la app y no devuelve el control hasta que alguien la cierra a mano: colgaba al que compilaba por tiempo indefinido. Se adopta el patron que ya usaba PipeSync: por defecto la app se lanza en **background** (`&` + `disown`) y el script termina enseguida imprimiendo el PID y la ruta del log, y se agrega **`--wait`** para recuperar el comportamiento viejo cuando hace falta ver un crash al arranque, el stdout/stderr o el **exit code**. El `--no-run` que ya existia se mantiene para compilar sin lanzar. En background el stdout/stderr se descarta porque el log completo va a `/tmp/ThetaMacExplorer.log` (override con `THETA_LOG_FILE`, que es lo que lee `Logger.cpp` — no `~/Library/Logs/LGA/` como las otras apps). Medido: sin flag el script vuelve en 1.2 s con la app corriendo. Se corrige tambien la seccion "Build" de las reglas del repo, que en v0.97 habia quedado recomendando `--no-run` como solucion al bloqueo cuando la solucion real era no bloquear.
- [ Build - compilar_dev.sh lanza en background por defecto y suma --wait ]

v0.98:
- Reglas: las reglas del repo se sincronizan; una de sus copias no habia recibido la reescritura de las secciones "Iconos de macOS" y "Build".
- [ Reglas - Sincronizar las reglas del repo ]

v0.97:
- Rules / build: se documenta que `compilar_dev.sh` **toma la terminal**. El script termina con `exec "$APP_BIN"`, o sea reemplaza el proceso del shell con la app y no devuelve el control hasta que la app se cierra; corrido en primer plano, se queda esperando para siempre. Se agregan a la seccion "Build" de las reglas del repo las dos formas correctas: `--no-run` (compila, deploya y refresca el cache de iconos sin lanzar la app — la opcion por defecto cuando solo hace falta validar el build o revisar el bundle) o lanzarlo en segundo plano cuando ademas se quiere la app corriendo. Se listan tambien los otros flags (`--force-clean`, `--parallel N`, `--no-deploy`, `--help`) y se aclara que el script ya refresca el cache de iconos del bundle, asi que no hace falta repetirlo a mano salvo que se toque el bundle despues de compilar.
- [ Rules - Documentar que compilar_dev.sh toma la terminal y usar --no-run ]

v0.96:
- Icono de macOS: pasa al pipeline de Icon Composer. El icono se disena en `resources/icons/Alta/ThetaMacExplorer_v003.icon` y se compila con `actool`, en vez del `ThetaExplorer.icns` generado con el pipeline de `../LGA_IconLab`. Van **dos** recursos a `Contents/Resources/` y hacen falta los dos: `Assets.car`, que es el icono real y lo que consume macOS 26 via `CFBundleIconName`, y `AppIcon.icns` como fallback via `CFBundleIconFile`. Convertir el `.icon` a un `.icns` suelto y descartar el CAR reintroduce el problema de tamano en Cmd+Tab. El `.icns` se genera con `--standalone-icon-behavior all`: por defecto `actool` mete solo 4 representaciones y llega hasta 256 px, con el flag son 10 y llega a 1024. `CMakeLists.txt` copia ambos y avisa con `message(WARNING)` si falta el CAR, porque sin el el icono cae al fallback sin romper el build. `cmake/Info.plist.in` suma `CFBundleIconName`. Se aprovecha para resincronizar la version: el `project(... VERSION ...)` estaba en `0.93` mientras el ChangeLog ya iba por `v0.95`, o sea las dos entradas anteriores no habian bumpeado el CMakeLists.
- [ Icono - El icono de macOS pasa a Icon Composer con Assets.car y icns de fallback ]

v0.95:
- Version / infra: se implementa el patron LGA "CMake single source". Se agrega `target_compile_definitions(ThetaMacExplorer PRIVATE THETAMACEXPLORER_VERSION="${PROJECT_VERSION}")` en `CMakeLists.txt`, y `src/main.cpp` cambia el hardcode `app.setApplicationVersion("0.93")` por `app.setApplicationVersion(QLatin1String(THETAMACEXPLORER_VERSION))`. Estado final: la version esta en UN solo lugar (`project(ThetaMacExplorer VERSION ...)` en CMakeLists), no requiere sincronizacion manual entre CMake y main.cpp. Las reglas del repo quedan actualizadas con la seccion "Fuente unica de verdad" al estado nuevo (ya no "pendiente de migrar").
- [ Version - CMake single source (THETAMACEXPLORER_VERSION macro) ]

v0.94:
- Reglas: las reglas del repo se alinean con el estandar unificado LGA (hub Qt/C++ y docs del Base, politica de idioma, Help/About, fuente unica de la version, autoria en GitHub), preservando lo propio del repo: versionado `0.XX` con bump `+0.01` por entrada, sincronizacion manual entre `CMakeLists.txt` y `main.cpp` hasta migrar a macro y el guard de SDK en `compilar_dev.sh`.
- [ Reglas - Unificar con el estandar LGA ]

v0.93:
- Build macOS: `compilar_dev.sh` y `compilar.sh` dejaban de reiniciar el extension host de VSCode al ejecutarse. Ambos usaban `pkill -f "$APP_NAME"` (con `APP_NAME="ThetaMacExplorer"`), un patrón demasiado genérico: `-f` matchea contra la command line completa de todos los procesos y puede pegar a extensiones/helpers de VSCode cuya command line contenga esa palabra. Se reemplaza por `pkill -f "${APP_NAME}.app/Contents/MacOS/${APP_NAME}"` (path completo del ejecutable dentro del `.app`), mismo criterio que usan las otras apps LGA. Se agrega comentario preventivo en ambos scripts para no volver al patrón genérico.
- [ Build macOS - Fix pkill genérico en compilar/compilar_dev que reiniciaba extensiones de VSCode ]

v0.92:
- Build macOS / AGL: arreglo de raiz del warning `ld: framework 'AGL' not found` (y del posterior mismatch de version cuando el sistema exponia una stub). El fallback `-framework AGL` lo agrega `WrapOpenGL::WrapOpenGL` de Qt6 por transitividad via `Qt6::Gui`. En `CMakeLists.txt` se agregan dos bloques: el preventivo `set(OPENGL_agl_LIBRARY "")` antes de `find_package(Qt6 ...)` y el purgador de AGL de `WrapOpenGL::WrapOpenGL` despues. Se elimina la linea `target_link_options(... "LINKER:-weak_framework,AGL")` que quedaba como parche antiguo. `compilar.sh` y `compilar_dev.sh` dejan de crear el dummy `AGL.framework` y de pasar `-F $PWD` al linker; `compilar_dev.sh` incluye migracion one-shot para limpiar el `-F` heredado en la cache.
- [ Build macOS - Fix AGL sin dummy framework ]

v0.91:
- macOS: nuevo icono con el estandar glass de la suite LGA — squircle plano al 80% de la grilla de Apple (Tahoe le agrega el rim de Liquid Glass), glyph al 74% con padding, y fondo gris igual a PipeSync (gradiente 46→28, nunca negro). El glyph se toma de la fuente SVG limpia (`ThetaExplorer.svg`) por saturacion, no del icono horneado, para no arrastrar el borde del squircle viejo.
- macOS build: se arregla el fallo `'type_traits' file not found` que venia de un `CMakeCache` viejo apuntando a un SDK borrado tras un update de Xcode. `compilar_dev.sh` ahora limpia el build si el SDK cacheado no existe, y refresca el cache de iconos del bundle (`touch` + `lsregister -f`) tras compilar.
- [ Mac - Icono glass + fix build SDK viejo y cache de iconos ]

v0.90:
- Version inicial registrada; se empieza a mantener changelog y reglas para el repo. [ Reglas - Se inicializa changelog y reglas de repo ]
