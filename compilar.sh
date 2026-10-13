#!/bin/bash

set -u

# Binario universal (arm64 + x86_64): en Apple Silicon corre nativo. Antes el script se
# relanzaba entero bajo Rosetta y compilaba solo x86_64, y macOS avisaba que la app Intel
# deja de funcionar en una version futura. Qt 6.5.3 ya trae sus frameworks universales.
# hw.optional.arm64 dice el hardware real: `uname -m` da x86_64 si la terminal corre bajo Rosetta.
if [ "$(sysctl -n hw.optional.arm64 2>/dev/null)" = "1" ]; then
    BUILD_ARCHS="arm64;x86_64"
else
    BUILD_ARCHS="x86_64"
fi

APP_NAME="LGA ThetaExplorer"
QT_PATH="$HOME/Qt/6.5.3/macos"
BUILD_DIR="build"
BUILD_TYPE="Debug"

show_help() {
    echo "Uso: $0 [--release] [--force-clean] [--parallel N] [--no-deploy] [--no-run] [--wait] [--force-rosetta]"
    echo ""
    echo "Opciones:"
    echo "  --release        Compila Release en build-release/ (lo usa deploy.sh). No toca"
    echo "                   build/ ni cierra la app de desarrollo."
    echo "  --force-clean    Borra build y reconfigura desde cero"
    echo "  --parallel N     Usa N núcleos para compilar"
    echo "  --no-deploy      Salta macdeployqt y copias de plugins"
    echo "  --no-run         Compila pero no lanza la app"
    echo "  --wait           Dejar la app en foreground: la terminal queda retenida hasta"
    echo "                   cerrarla y se ven su stdout/stderr y su exit code."
    echo "                   Por defecto la app se lanza en background y el script termina."
    echo "  --force-rosetta  Lanzar la app bajo Rosetta (x86_64) en Apple Silicon, para probar"
    echo "                   la mitad Intel del binario universal."
}

FORCE_CLEAN=false
NO_DEPLOY=false
NO_RUN=false
# Por defecto la app se lanza en BACKGROUND y el script termina enseguida. Dejarla en
# foreground retiene la terminal hasta que alguien cierre la app a mano, lo que en la
# practica cuelga al que compila por tiempo indefinido.
# Con --wait se recupera el comportamiento viejo, util para ver un crash o un exit code.
WAIT_FOR_APP=false
FORCE_ROSETTA=false
RELEASE=false
PARALLEL_CORES=$(sysctl -n hw.logicalcpu)

while [[ $# -gt 0 ]]; do
    case "$1" in
        --force-clean)
            FORCE_CLEAN=true
            shift
            ;;
        --parallel)
            PARALLEL_CORES="$2"
            shift 2
            ;;
        --no-deploy)
            NO_DEPLOY=true
            shift
            ;;
        --no-run)
            NO_RUN=true
            shift
            ;;
        --wait)
            WAIT_FOR_APP=true
            shift
            ;;
        --force-rosetta)
            FORCE_ROSETTA=true
            shift
            ;;
        --release)
            RELEASE=true
            shift
            ;;
        --help)
            show_help
            exit 0
            ;;
        *)
            echo "Opción desconocida: $1"
            show_help
            exit 1
            ;;
    esac
done

# Release va en un arbol SEPARADO: deployar no invalida la cache incremental de desarrollo.
if [ "$RELEASE" = "true" ]; then
    BUILD_DIR="build-release"
    BUILD_TYPE="Release"
fi
APP_BUNDLE="$BUILD_DIR/$APP_NAME.app"
APP_BIN="$APP_BUNDLE/Contents/MacOS/$APP_NAME"

echo "=== $BUILD_TYPE build for $APP_NAME ($PARALLEL_CORES cores) ==="

if [ "$FORCE_CLEAN" = "true" ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# Matar SOLO el ejecutable del bundle de este proyecto.
# IMPORTANTE: no usar `pkill -f "$APP_NAME"` (patrón demasiado genérico:
# matchea cualquier proceso cuya command line contenga la palabra —incluidas
# extensiones/helpers de VSCode— y hace que VSCode las relance varias
# veces al arrancar el script). Apuntar al path completo del ejecutable
# dentro del .app.
# En --release no hace falta: compila en build-release/ y no pisa el binario que corre.
if [ "$RELEASE" = "false" ]; then
    pkill -f "${APP_NAME}.app/Contents/MacOS/${APP_NAME}" 2>/dev/null || true
    sleep 0.3
fi

# Guard contra cache viejo de SDK: si el CMakeCache apunta a un CMAKE_OSX_SYSROOT
# (MacOSXNN.sdk) que ya no existe (tipico tras un update de Xcode que cambia la version
# del SDK), CMake seguiria pasando un -isysroot inexistente y el build falla con
# "'type_traits' file not found". En ese caso limpiamos el build para reconfigurar solo.
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    CACHED_SDK="$(awk -F= '/^CMAKE_OSX_SYSROOT/{print $2}' "$BUILD_DIR/CMakeCache.txt")"
    if [ -n "$CACHED_SDK" ] && [ ! -d "$CACHED_SDK" ]; then
        echo "SDK cacheado no existe ($CACHED_SDK); limpiando build para reconfigurar..."
        rm -rf "$BUILD_DIR"
    fi
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR" || exit 1

# El requerimiento de -framework AGL de Qt6 (via WrapOpenGL::WrapOpenGL) se
# neutraliza directamente en CMakeLists.txt, asi que no hace falta dummy AGL.
CMAKE_FLAGS=(
    -G "Unix Makefiles"
    -DCMAKE_PREFIX_PATH="$QT_PATH"
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
    -DCMAKE_OSX_ARCHITECTURES="$BUILD_ARCHS"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DQt6_DIR="$QT_PATH/lib/cmake/Qt6"
    -DCMAKE_CXX_FLAGS_DEBUG="-g -O0 -Wno-unused-parameter"
)

# Migración: si la cache trae "-F <build>" del viejo dummy AGL, forzar
# reconfiguración para que se limpie ese flag obsoleto.
NEEDS_RECONFIGURE=false
if [ ! -f "CMakeCache.txt" ] || [ "$FORCE_CLEAN" = "true" ]; then
    NEEDS_RECONFIGURE=true
else
    CACHED_FLAGS=$(awk -F= '/^CMAKE_EXE_LINKER_FLAGS:STRING=/{print $2}' CMakeCache.txt 2>/dev/null || true)
    if [[ "$CACHED_FLAGS" == *"-F "* ]]; then
        echo "Cleaning stale -F flag from previous AGL dummy, reconfiguring..."
        NEEDS_RECONFIGURE=true
    fi
    # Un build configurado con otras arquitecturas (p. ej. el x86_64 de antes) se reconfigura;
    # no hace falta borrar build/, CMake recompila todo solo.
    CACHED_TYPE=$(awk -F= '/^CMAKE_BUILD_TYPE:/{print $2}' CMakeCache.txt 2>/dev/null || true)
    if [ "$CACHED_TYPE" != "$BUILD_TYPE" ]; then
        echo "Build type changed ($CACHED_TYPE -> $BUILD_TYPE), reconfiguring..."
        NEEDS_RECONFIGURE=true
    fi
    CACHED_ARCHS=$(awk -F= '/^CMAKE_OSX_ARCHITECTURES:/{print $2}' CMakeCache.txt 2>/dev/null || true)
    if [ "$CACHED_ARCHS" != "$BUILD_ARCHS" ]; then
        echo "Architectures changed ($CACHED_ARCHS -> $BUILD_ARCHS), reconfiguring..."
        NEEDS_RECONFIGURE=true
    fi
fi

if [ "$NEEDS_RECONFIGURE" = "true" ]; then
    echo "Configuring CMake..."
    cmake .. "${CMAKE_FLAGS[@]}" || exit 1
fi

echo "Building..."
cmake --build . --parallel "$PARALLEL_CORES" || exit 1

cd .. || exit 1

if [ "$NO_DEPLOY" = "false" ]; then
    export PATH="$QT_PATH/bin:$PATH"

    mkdir -p "$BUILD_DIR/local_bin"
    if [ ! -f "$BUILD_DIR/local_bin/otool-classic" ]; then
        OTOOL_PATH=$(xcrun -f otool)
        ln -sf "$OTOOL_PATH" "$BUILD_DIR/local_bin/otool-classic"
    fi
    export PATH="$PWD/$BUILD_DIR/local_bin:$PATH"

    NEEDS_DEPLOY=false
    if [ ! -d "$APP_BUNDLE/Contents/Frameworks" ]; then
        NEEDS_DEPLOY=true
    elif [ ! -f "$APP_BUNDLE/Contents/PlugIns/platforms/libqcocoa.dylib" ]; then
        NEEDS_DEPLOY=true
    elif [ ! -f "$APP_BUNDLE/Contents/PlugIns/imageformats/libqjpeg.dylib" ]; then
        NEEDS_DEPLOY=true
    elif [ ! -d "$APP_BUNDLE/Contents/Frameworks/QtNetwork.framework" ]; then
        NEEDS_DEPLOY=true
    elif [ ! -f "$APP_BUNDLE/Contents/PlugIns/tls/libqsecuretransportbackend.dylib" ]; then
        # QtNetwork puede estar por transitividad sin su backend HTTPS en un bundle viejo.
        NEEDS_DEPLOY=true
    elif [ ! -d "$APP_BUNDLE/Contents/Frameworks/QtSvg.framework" ]; then
        # Los iconos de la UI se rasterizan con QtSvg: un bundle armado antes de sumarlo
        # no lo trae y la app no arranca.
        NEEDS_DEPLOY=true
    fi

    if [ "$NEEDS_DEPLOY" = "true" ]; then
        echo "Deploying minimal Qt runtime..."
        "$QT_PATH/bin/macdeployqt" "$APP_BUNDLE" || exit 1

        if [ ! -f "$APP_BUNDLE/Contents/PlugIns/platforms/libqcocoa.dylib" ]; then
            mkdir -p "$APP_BUNDLE/Contents/PlugIns/platforms"
            cp "$QT_PATH/plugins/platforms/"* "$APP_BUNDLE/Contents/PlugIns/platforms/" 2>/dev/null || true
        fi

        mkdir -p "$APP_BUNDLE/Contents/PlugIns/imageformats"
        for fmt in libqjpeg.dylib libqpng.dylib libqtiff.dylib libqwebp.dylib; do
            [ -f "$QT_PATH/plugins/imageformats/$fmt" ] && \
                cp "$QT_PATH/plugins/imageformats/$fmt" "$APP_BUNDLE/Contents/PlugIns/imageformats/" || true
        done
    else
        echo "Qt runtime already present. Skipping deploy."
    fi
else
    echo "Skipping deploy (--no-deploy)"
fi

# El bundle tiene que quedar entero universal: un framework o plugin de una sola
# arquitectura hace que la app no arranque en la otra.
if [ "$NO_DEPLOY" = "false" ] && [ "$BUILD_ARCHS" = "arm64;x86_64" ]; then
    bash "./tools/macos/validate_universal_macho.sh" "$APP_BUNDLE" || \
        echo "WARNING: el bundle tiene binarios que no son universales (ver arriba)."
fi

# TCC exige una identidad valida para acceder a la camara; el linker no firma el bundle.
CODESIGN_IDENTITY="${LGA_CODESIGN_IDENTITY:-LGA Code Signing}"
if security find-identity -v -p codesigning 2>/dev/null | grep -Fq "\"$CODESIGN_IDENTITY\""; then
    echo "Firmando con '$CODESIGN_IDENTITY'..."
    codesign --force --deep --sign "$CODESIGN_IDENTITY" "$APP_BUNDLE" || exit 1
else
    echo "Firmando ad-hoc (no hay identidad '$CODESIGN_IDENTITY' en el llavero)..."
    codesign --force --deep --sign - "$APP_BUNDLE" || exit 1
fi
codesign --verify --deep --strict "$APP_BUNDLE" || exit 1

echo "Build complete."

# Refrescar el cache de iconos del bundle: tras cambiar el .icns, el Dock/Finder pueden
# seguir mostrando el icono viejo por cache (iconservices). touch + lsregister -f fuerzan
# a re-leer el icono. Si el cache de Tahoe sigue pegajoso, cerrar sesion y volver a entrar.
LSREG="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
if [ -d "$APP_BUNDLE" ]; then
    touch "$APP_BUNDLE"
    [ -x "$LSREG" ] && "$LSREG" -f "$APP_BUNDLE" >/dev/null 2>&1 || true
fi

if [ "$NO_RUN" = "true" ]; then
    exit 0
fi

if [ ! -x "$APP_BIN" ]; then
    echo "ERROR: Missing app executable at $APP_BIN"
    exit 1
fi

echo "Launching $APP_NAME..."

# Por defecto se lanza en BACKGROUND y el script termina enseguida. Dejarla en foreground
# retiene la terminal hasta que alguien cierra la app a mano, lo que en la practica cuelga
# al que compila por tiempo indefinido. El stdout/stderr de la app
# se descarta: el log completo ya va al archivo de Logger (por defecto
# ~/Library/Logs/LGA/ThetaExplorer/debug.log, override con la variable de entorno THETA_LOG_FILE).
#
# Con --wait se mantiene el comportamiento viejo, que es el que sirve para ver un crash al
# arranque, el exit code o los prints que todavia no pasan por el log.
LAUNCH=("$APP_BIN")
if [ "$FORCE_ROSETTA" = "true" ] && [ "$BUILD_ARCHS" = "arm64;x86_64" ]; then
    echo "   (bajo Rosetta, x86_64)"
    LAUNCH=(arch -x86_64 "$APP_BIN")
fi

if [ "$WAIT_FOR_APP" = "true" ]; then
    exec "${LAUNCH[@]}"
else
    "${LAUNCH[@]}" >/dev/null 2>&1 &
    disown
    echo "   PID $! (background). Log: ${THETA_LOG_FILE:-$HOME/Library/Logs/LGA/ThetaExplorer/debug.log}"
    echo "   Usá --wait si necesitás ver su salida o su exit code en la terminal."
fi
