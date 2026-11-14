#!/bin/bash
set -euo pipefail

# Deploy de ThetaExplorer para macOS, en un solo paso:
#   1. compila Release en build-release/ (universal arm64 + x86_64),
#   2. copia el bundle a deploy/, lo valida y lo firma,
#   3. arma el ZIP (actualizacion) y/o el DMG (primera instalacion),
#   4. opcionalmente los publica en el release v<version> de GitHub (tools/macos/publish_release.sh),
#   5. instala la app en /Applications y la abre.
#
# Con terminal pregunta TODO al principio y despues corre sin frenar. La pregunta de publicar va
# primero y por defecto es NO: un release publico no se deshace. Sin terminal (pipe, CI) no
# pregunta nada: hace solo lo que digan los flags.
#
# Esquema de LGA_Base_QT_C_Py (deploy.sh, docs/Doc_Deploy_macOS.md). Este repo es PUBLICO: el
# release va en el mismo repo.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

APP_NAME="LGA ThetaExplorer"            # nombre del .app en Applications
ARTIFACT_NAME="LGA_ThetaExplorer"        # base del .zip/.dmg: sin espacios, viaja por URL
BUILD_DIR="build-release"

show_help() {
    echo "Uso: $0 [--zip] [--dmg] [--publish [--dry-run] [--replace] [--yes] | --no-publish]"
    echo "         [--no-notarize] [--no-install] [--no-open] [--no-open-finder] [--parallel N]"
    echo ""
    echo "Sin flags y con terminal, mira GitHub y pregunta ANTES de compilar: publicar (o sumar los"
    echo "archivos de macOS al release), el bump si esa version ya esta publicada o falta, build local"
    echo "o cancelar; y si no se publica, por el ZIP y el DMG. Todo con una tecla, sin Enter."
    echo ""
    echo "  --zip             Crear deploy/${ARTIFACT_NAME}_Mac_v<version>.zip (actualizacion)"
    echo "  --dmg             Crear deploy/${ARTIFACT_NAME}_Mac_v<version>.dmg (primera instalacion)"
    echo "  --publish         Publicar el .zip y el .dmg en el release v<version> de GitHub (implica --zip --dmg)"
    echo "  --no-publish      No publicar ni preguntar"
    echo "  --dry-run         Con --publish: todo menos escribir en GitHub y en el repo"
    echo "  --replace         Con --publish: reemplazar los archivos de macOS si el release ya los tiene"
    echo "  --yes             Con --publish: sin efecto en esta app (solo saltea el control del What's new, que no usa)"
    echo "  --no-notarize     No mandar a notarizar el ZIP ni el DMG (salen pidiendo xattr)"
    echo "  --no-install      No instalar en /Applications ni abrir la app (deja solo deploy/)"
    echo "  --no-open         Instalar en /Applications pero no abrir la app"
    echo "  --no-open-finder  No mostrar el resultado en Finder al terminar"
    echo "  --parallel N      Usar N nucleos para compilar"
}

ZIP_MODE="prompt"; DMG_MODE="prompt"; PUBLISH_MODE="prompt"
PUBLISH_ARGS=(); NO_OPEN_FINDER=false
INSTALL_APP=true; OPEN_APP=true
# Notarizar cuando hay Developer ID y se arma un ZIP o un DMG. LGA_NOTARIZE=0 lo apaga igual que
# --no-notarize (sirve para deploy_local.sh sin red, o para no esperar a Apple en una prueba).
NOTARIZE=true
[ "${LGA_NOTARIZE:-1}" = "0" ] && NOTARIZE=false
PARALLEL_CORES="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --zip) ZIP_MODE="always"; shift ;;
        --dmg) DMG_MODE="always"; shift ;;
        --publish)
            [[ "$PUBLISH_MODE" != "never" ]] || { echo "ERROR: --publish y --no-publish se contradicen."; exit 1; }
            PUBLISH_MODE="always"; shift ;;
        --no-publish)
            [[ "$PUBLISH_MODE" != "always" ]] || { echo "ERROR: --publish y --no-publish se contradicen."; exit 1; }
            PUBLISH_MODE="never"; shift ;;
        --dry-run|--replace|--yes) PUBLISH_ARGS+=("$1"); shift ;;
        -y) PUBLISH_ARGS+=(--yes); shift ;;
        --no-notarize) NOTARIZE=false; shift ;;
        --no-install) INSTALL_APP=false; shift ;;
        --no-open) OPEN_APP=false; shift ;;
        --no-open-finder) NO_OPEN_FINDER=true; shift ;;
        --parallel)
            [[ $# -ge 2 ]] || { echo "ERROR: --parallel requiere un valor."; exit 1; }
            PARALLEL_CORES="$2"; shift 2 ;;
        -h|--help) show_help; exit 0 ;;
        *) echo "Opcion desconocida: $1"; show_help; exit 1 ;;
    esac
done

if ! [[ "$PARALLEL_CORES" =~ ^[0-9]+$ ]] || [[ "$PARALLEL_CORES" -lt 1 ]]; then
    echo "ERROR: --parallel debe ser un entero mayor o igual a 1."
    exit 1
fi
if [[ "$PUBLISH_MODE" != "always" && ${#PUBLISH_ARGS[@]} -gt 0 ]]; then
    echo "ERROR: ${PUBLISH_ARGS[*]} solo tiene sentido con --publish."
    exit 1
fi
if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "ERROR: deploy.sh es solo para macOS."
    exit 1
fi

INTERACTIVE=false
[[ -t 0 ]] && INTERACTIVE=true

# Toda pregunta se contesta con UNA tecla, sin Enter (regla LGA, 2026-10-17): una tecla que no es
# Y/N no hace nada, y no hay respuesta por defecto (Enter tampoco hace nada). Ctrl+C sigue cortando.
ask_yes_no() {  # <pregunta>; exit 0 = Y
    local k
    printf '%s [Y/N] ' "$1"
    while true; do
        IFS= read -rsn1 k || return 1
        case "$k" in
            [Yy]) echo "Y"; return 0 ;;
            [Nn]) echo "N"; return 1 ;;
        esac
    done
}

decide() {
    # always -> si; prompt con terminal -> pregunta; prompt sin terminal -> no.
    local mode="$1" prompt="$2"
    if [[ "$mode" == "always" ]]; then return 0; fi
    if [[ "$INTERACTIVE" == "true" ]]; then ask_yes_no "$prompt"; return $?; fi
    return 1
}

# ---- Version: CMakeLists.txt guarda la ultima PUBLICADA y el ChangeLog la EN DESARROLLO ----
# El numero sube solo con ./bump_version.sh, que se corre al decidir publicar (lo ofrece la
# pregunta de abajo). Sin el bump, el deploy local se arma con la version publicada.
read_app_version() {
    APP_VERSION="$(sed -n 's/^[[:space:]]*project(.*VERSION[[:space:]]\{1,\}\([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
    if ! [[ "$APP_VERSION" =~ ^[0-9]+(\.[0-9]+)+$ ]]; then
        echo "ERROR: no se pudo leer la version de CMakeLists.txt (project(... VERSION ...))."
        exit 1
    fi
}
read_app_version

# ---- Publicar: se decide ACA, antes de compilar, con tools/macos/publish_release.sh ----
# La pieza canonica de LGA_Base_QT_C_Py (copia IDENTICA; lo propio de esta app, titulo, notas y
# BUMP_CMD incluidos, esta en tools/macos/release.conf). Contrato:
# ../LGA_Base_QT_C_Py/docs/Doc_Deploy_macOS.md §5.
#   - sin --publish ni --no-publish y con terminal, `--decide` mira el release en GitHub y pregunta
#     con UNA tecla: publicar o sumar los archivos de macOS al release que ya existe, el bump si esa
#     version ya esta publicada o falta, build local o cancelar. Si vuelve con "publicar", el
#     preflight ya paso, con la version que haya dejado el bump.
#   - con --publish, `--preflight` directo (gh y la identidad, arbol limpio, el bump, el tag, el
#     release, repo publico): si algo impide publicar, corta sin gastar el build.
#   - con el ZIP y el DMG armados, `--expect-head` vuelve a comprobar todo (Windows pudo publicar
#     en el medio) y publica.
PUBLISH=false
if [[ "$PUBLISH_MODE" == "always" ]]; then
    PUBLISH=true
    bash ./tools/macos/publish_release.sh --preflight ${PUBLISH_ARGS[@]+"${PUBLISH_ARGS[@]}"}
elif [[ "$PUBLISH_MODE" == "prompt" && "$INTERACTIVE" == "true" && "$ZIP_MODE" == "prompt" && "$DMG_MODE" == "prompt" ]]; then
    DECIDE_RC=0
    bash ./tools/macos/publish_release.sh --decide || DECIDE_RC=$?
    case "$DECIDE_RC" in
        0)
            PUBLISH=true
            read_app_version   # el bump pudo cambiarla
            ;;
        10) ;;
        20)
            echo "Cancelado. No se compilo nada."
            exit 1
            ;;
        *) echo "AVISO: no se pudo decidir la publicacion (ver arriba): se sigue con el build local." ;;
    esac
fi
PUBLISH_HEAD=""
[[ "$PUBLISH" == "true" ]] && PUBLISH_HEAD="$(git rev-parse HEAD)"

TAG="v${APP_VERSION}"
ZIP_NAME="${ARTIFACT_NAME}_Mac_v${APP_VERSION}.zip"
DMG_NAME="${ARTIFACT_NAME}_Mac_v${APP_VERSION}.dmg"
APP_PATH="deploy/${APP_NAME}.app"

echo "Deploy de $APP_NAME v$APP_VERSION"
echo ""

# ---- El resto de las preguntas, al principio. Publicar implica el ZIP y el DMG ----
CREATE_ZIP=false; CREATE_DMG=false
if [[ "$PUBLISH" == "true" ]]; then
    CREATE_ZIP=true; CREATE_DMG=true
else
    decide "$ZIP_MODE" "Crear el ZIP (actualizacion)?" && CREATE_ZIP=true
    decide "$DMG_MODE" "Crear el DMG (primera instalacion)?" && CREATE_DMG=true
fi

# ---- Build Release ----
# Arbol separado (build-release/) y CON macdeployqt: lo que se publica va optimizado y con
# todos sus frameworks, y no invalida la cache incremental de desarrollo.
./compilar.sh --release --no-run --parallel "$PARALLEL_CORES"

if [[ ! -d "${BUILD_DIR}/${APP_NAME}.app" ]]; then
    echo "ERROR: no se encontro ${BUILD_DIR}/${APP_NAME}.app"
    exit 1
fi
BUILT_TYPE="$(sed -n 's/^CMAKE_BUILD_TYPE:[^=]*=//p' "${BUILD_DIR}/CMakeCache.txt" | head -1)"
if [[ "$BUILT_TYPE" != "Release" ]]; then
    echo "ERROR: ${BUILD_DIR} quedo en '${BUILT_TYPE:-?}', no en Release."
    exit 1
fi

# ditto y no `cp -R`: conserva symlinks, permisos y metadata del bundle.
mkdir -p deploy
rm -rf "$APP_PATH"
ditto "${BUILD_DIR}/${APP_NAME}.app" "$APP_PATH"

# Un binario de una sola arquitectura hace que la app no arranque en la otra Mac.
if ! bash ./tools/macos/validate_universal_macho.sh "$APP_PATH"; then
    echo "ERROR: el bundle tiene binarios que no son universales."
    exit 1
fi

# Firma del bundle YA armado: la firma cubre el contenido, asi que va despues de copiar todo
# adentro. La hace tools/macos/sign_bundle.sh (pieza canonica, copia IDENTICA), que elige la
# identidad: Developer ID si esta en el llavero (hardened runtime, sello de tiempo, de adentro
# hacia afuera: es lo que se puede notarizar), si no "LGA Code Signing" (estable para los
# permisos de TCC, sin notarizar) y si no ad-hoc. Con cualquiera de las tres, `codesign --verify`
# comprueba despues que el bundle llego entero, que es lo que hace el updater.
# Los entitlements de la app, si los necesita con hardened runtime, van en
# resources/macos/Entitlements.plist (la camara: ver el comentario del archivo).
ENTITLEMENTS_FILE="resources/macos/Entitlements.plist"
SIGN_ARGS=()
[ -f "$ENTITLEMENTS_FILE" ] && SIGN_ARGS+=(--entitlements "$ENTITLEMENTS_FILE")
bash "./tools/macos/sign_bundle.sh" "$APP_PATH" ${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"}
bash "./tools/macos/verify_bundle_signature.sh" "$APP_PATH"
SIGN_KIND="$(bash "./tools/macos/sign_bundle.sh" --kind)"
echo "Bundle listo: $APP_PATH"

# Notarizacion (tools/macos/notarize.sh, pieza canonica): primero el .app, asi el ticket queda
# abrochado adentro del ZIP y del DMG; despues el DMG, que es lo que baja el usuario. Sin eso
# Gatekeeper no deja abrir la app en otra Mac sin el `xattr -cr`.
# Un rechazo de Apple corta una publicacion; en un build local avisa y sigue.
NOTARIZED=false
notarize_or_warn() {  # <ruta>; 0 = notarizado
    local rc=0
    bash "./tools/macos/notarize.sh" "$1" || rc=$?
    [ $rc -eq 0 ] && return 0
    if [ $rc -eq 1 ] && [ "$PUBLISH" = "true" ]; then
        echo "ERROR: no se publica un release que Apple rechazo."
        exit 1
    fi
    echo "AVISO: $(basename "$1") sale SIN notarizar: en otra Mac va a pedir el 'xattr -cr'."
    return 1
}
if { [ "$CREATE_ZIP" = "true" ] || [ "$CREATE_DMG" = "true" ]; } && [ "$SIGN_KIND" = "developer-id" ]; then
    if [ "$NOTARIZE" = "true" ]; then
        notarize_or_warn "$APP_PATH" && NOTARIZED=true
        bash "./tools/macos/verify_bundle_signature.sh" "$APP_PATH"
    else
        echo "AVISO: --no-notarize: el ZIP y el DMG salen sin notarizar (piden el 'xattr -cr')."
    fi
fi


if [[ "$CREATE_ZIP" == "true" ]]; then
    # ditto y NO zip: `zip -r` resuelve los symlinks de los frameworks de Qt, duplica cada
    # uno y rompe la firma.
    rm -f "deploy/${ZIP_NAME}"
    bash "./tools/macos/verify_bundle_signature.sh" "$APP_PATH"
    (cd deploy && ditto -c -k --sequesterRsrc --keepParent "${APP_NAME}.app" "${ZIP_NAME}")
    VERIFY_DIR="$(mktemp -d "${TMPDIR:-/tmp}/${ARTIFACT_NAME}_verify_XXXXXX")"
    ditto -x -k "deploy/${ZIP_NAME}" "$VERIFY_DIR"
    SYMLINKS="$(find "$VERIFY_DIR/${APP_NAME}.app" -type l | wc -l | tr -d ' ')"
    if [[ "$SYMLINKS" -eq 0 ]] || ! codesign --verify --deep --strict "$VERIFY_DIR/${APP_NAME}.app" 2>/dev/null; then
        rm -rf "$VERIFY_DIR"
        echo "ERROR: el ZIP no conserva los symlinks o la firma no verifica."
        exit 1
    fi
    rm -rf "$VERIFY_DIR"
    echo "ZIP listo: deploy/${ZIP_NAME} (${SYMLINKS} symlinks, firma valida)"
fi
if [[ "$CREATE_DMG" == "true" ]]; then
    bash ./create_dmg.sh --no-open
    if [ "$NOTARIZED" = "true" ]; then
        bash "./tools/macos/sign_bundle.sh" "deploy/${DMG_NAME}"
        notarize_or_warn "deploy/${DMG_NAME}" || true
    fi
fi


# ---- Publicacion ----
# Si falla, igual se instala la app (el build esta bien) y el script sale con error al final.
PUBLISH_FAILED=false
if [[ "$PUBLISH" == "true" ]]; then
    if ! bash ./tools/macos/publish_release.sh --expect-head "$PUBLISH_HEAD" ${PUBLISH_ARGS[@]+"${PUBLISH_ARGS[@]}"}; then
        PUBLISH_FAILED=true
    fi
fi

# El final de todo deploy: instalar en /Applications y abrir la copia INSTALADA. El .app de
# deploy/ no se ejecuta nunca: es el que se empaqueta, y ejecutarlo le rompe la firma.
if [[ "$INSTALL_APP" == "true" ]]; then
    INSTALL_ARGS=()
    [[ "$OPEN_APP" == "false" ]] && INSTALL_ARGS+=(--no-open)
    bash "./tools/macos/install_to_applications.sh" "$APP_PATH" ${INSTALL_ARGS[@]+"${INSTALL_ARGS[@]}"}
fi

if [[ "$NO_OPEN_FINDER" == "false" && "$INTERACTIVE" == "true" ]]; then
    ask_yes_no "Mostrar deploy/ en Finder?" "N" && open "deploy"
fi
if [[ "$PUBLISH_FAILED" == "true" ]]; then
    echo "ERROR: la publicacion del release $TAG fallo (ver arriba)."
    [[ "$INSTALL_APP" == "true" ]] && echo "       La app quedo instalada igual."
    exit 1
fi
echo "Deploy terminado."
