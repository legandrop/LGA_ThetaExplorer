<p align="right"><a href="README.md">English</a> · <b>Español</b></p>

# Theta Explorer

Explorá y descargá las fotos y videos de una cámara **RICOH THETA** desde macOS o Windows, por USB.
Pensada para trabajar con HDRIs: cada secuencia HDR aparece como **un solo ítem**, con la
vista previa de la exposición del medio, y se ve de un vistazo qué sets ya descargaste.

![Theta Explorer con una RICOH THETA Z1 conectada: cada set HDR en un solo tile, los badges Saved y Partial y la vista previa del set seleccionado](doc/images/thetaexplorer_main.jpg)

## Qué hace

- **Cada set HDR es un ítem.** Las secuencias DNG y JPG de la misma captura se agrupan en un
  solo tile con su cantidad de exposiciones. Descargar o borrar ese ítem incluye ambos formatos.
  También reconoce el bracket nativo por sus metadatos de exposición, aunque demore entre tomas
  o use un orden personalizado. El reinicio del patrón y las pausas separan capturas consecutivas.
- **Miniaturas RAW** desde el preview embebido cuando la cámara no entrega una miniatura.
- **Vista previa y datos** del ítem seleccionado: nombre, tipo, tamaño, fecha, resolución y
  ruta en la cámara.
- **Preview panorámico** debajo del JPEG original. Las fotos DualFisheye se unen de forma
  aproximada; las imágenes ya unidas conservan su panorama. La vista tiene costuras visibles,
  sin calibración óptica ni mezcla de las exposiciones HDR.
- **Historial permanente.** Las descargas verificadas se marcan *Downloaded*, o
  *Partial* cuando se descargó solo parte de los archivos. File Info muestra la fecha
  de la última descarga. Las marcas se conservan al mover o borrar la carpeta de destino.
- **Lo reciente resalta.** La fecha de una captura arranca en verde claro (ámbar en DNG) y
  se apaga a lo largo de 28 horas, así una sesión recién hecha se encuentra enseguida.
- **Carpetas por formato.** Los archivos DNG y JPG conservan sus propias carpetas, con
  la fecha de captura y sus archivos, por ejemplo `HDRI_260402_R21381-21389_dng`.
- **Volver a descargar sin pisar.** Si algunos archivos ya están en disco, podés saltearlos
  y bajar solo lo que falta (así se completa un set a medio bajar) o reemplazar la copia
  local.
- **Borrar de la cámara** con una confirmación que dice cuántos archivos se van. Enter
  cancela; borrar siempre pide un click.
- **Controles arriba del Viewer**, dejando toda la altura para las tarjetas. Cámara,
  batería, Refresh, Help, destino, Download y Delete quedan juntos.
- **Búsqueda automática de actualizaciones**, con comprobación manual y una preferencia
  para activarla o desactivarla en Help. Vos elegís cuándo descargar e instalar una versión nueva.

## Requisitos

- macOS 12 o posterior, en Apple Silicon o Intel.
- Una RICOH THETA conectada por USB y encendida. Probada con la **THETA Z1**.

## Instalación

Este repositorio nuevo todavía no tiene paquetes de release publicados. Se puede compilar
con los scripts del código fuente. Los paquetes futuros estarán en Releases.

1. Bajá `LGA_ThetaExplorer_Mac_v<version>.dmg` de
   [Releases](../../releases/latest).
2. Abrilo y arrastrá **LGA ThetaExplorer** a **Applications**.
3. Abrí la app desde Applications.

La app no está notarizada por Apple. Si macOS dice que no se puede abrir o que está
dañada, corré esto en Terminal y abrila de nuevo:

```bash
sudo xattr -cr "/Applications/LGA ThetaExplorer.app"
```

Solo le quita a esa copia de la app la marca de cuarentena que macOS pone a lo que se
descarga. No desactiva Gatekeeper.

La primera vez, macOS puede pedir permiso para acceder a la cámara: la app lo necesita
para leer los archivos de la THETA.

## Uso

1. Conectá la cámara. Sus fotos y videos aparecen como tiles; las secuencias HDR, como un
   solo tile.
2. Hacé clic en el destino **Save to**, arriba del Viewer, para elegir dónde guardar.
3. Seleccioná uno o más ítems y apretá **Download**, o **Delete** para borrarlos de la
   cámara.

| Atajo | Acción |
|---|---|
| `⌘A` / `Ctrl+A` | Seleccionar todo |
| `Esc` | Deseleccionar |
| `⌘?` / `F1` | Abrir la ayuda |

La ventana de Help muestra también dónde guarda la app sus archivos, y los abre en Finder:

- Settings: `~/Library/Application Support/LGA/ThetaExplorer/`
- Log: `~/Library/Logs/LGA/ThetaExplorer/debug.log`

El historial vive en `download-history.json`, junto a settings
(`%APPDATA%\LGA\ThetaExplorer` en Windows). Es propio de esta computadora,
distingue cada camara y empieza con las descargas hechas desde esta version.
Los archivos existentes y los previews no crean registros historicos. La marca
no saltea una nueva descarga: Skip Existing revisa los archivos del destino elegido.

En Help, **Check now** busca una versión publicada más nueva. La búsqueda automática está
activada inicialmente. Cuando hay una actualización, podés elegir **Update now**, **Remind
me later**, **Skip this version** o **Cancel**. Se actualizan las copias instaladas; las copias
dentro del árbol de fuentes o de build conservan su versión de desarrollo. Antes de instalar,
terminá las descargas o borrados de cámara y cerrá las otras copias de la app.

## Compilar

Hace falta Xcode command line tools, CMake y Qt 6.5.3 en `~/Qt/6.5.3/macos`.

```bash
./compilar.sh
```

compila un binario Debug universal (arm64 + x86_64) en `build/`, lo firma y lo lanza en
background. `--no-run` compila sin lanzar, `--wait` espera hasta cerrar la app y
`--release` compila Release en `build-release/`. `./deploy.sh` compila la versión
Release y arma el DMG.

## Créditos

Los HDRIs de la captura son de [Poly Haven](https://polyhaven.com) (CC0): Spiaggia di Mondello,
Sunflowers, Venice Sunset, Golden Gate Hills, Autumn Park, The Sky Is On Fire, Shanghai Bund,
Canary Wharf, Fireplace y Photo Studio Loft Hall.

## Autor

Desarrollado por Lega Pugliese — [lega.com.ar](https://lega.com.ar)

## Windows

Windows 10/11 de 64 bits usa Windows Portable Devices. Compilar con Qt 6.5.3 `mingw_64`,
MinGW 13.1 y Ninja mediante `compilar.bat`, que conserva Debug/Release en carpetas separadas
y abre la app sin retener la terminal. La validación con la cámara física en Windows sigue
pendiente. Detalle: [backend, scripts y límites](doc/windows.md).
