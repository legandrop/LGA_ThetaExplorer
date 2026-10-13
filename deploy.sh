#!/bin/bash
set -euo pipefail

# Deploy de ThetaExplorer para macOS, en un solo paso:
#   1. compila Release en build-release/ (universal arm64 + x86_64),
#   2. copia el bundle a deploy/, lo valida y lo firma,
#   3. arma el ZIP (actualizacion) y/o el DMG (primera instalacion),
#   4. opcionalmente crea el tag v<version> y publica el release en GitHub con esos assets.
#
# Con terminal pregunta TODO al principio y despues corre sin frenar, salvo la confirmacion
# final antes de publicar (que por defecto es NO: un release publico no se deshace). Sin
# terminal (pipe, CI) no pregunta nada: hace solo lo que digan los flags.
#
# Esquema copiado de LGA_FrameRev (deploy.sh) y LGA_Calmate (github_release_mac.sh), que
# siguen LGA_Base_QT_C_Py. Este repo es PUBLICO: el release va en el mismo repo.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

APP_NAME="LGA ThetaExplorer"            # nombre del .app en Applications
ARTIFACT_NAME="LGA_ThetaExplorer"        # base del .zip/.dmg: sin espacios, viaja por URL
RELEASE_REPO="${LGA_RELEASE_REPO:-legandrop/LGA_ThetaExplorer}"
BUILD_DIR="build-release"

show_help() {
    echo "Uso: $0 [--zip] [--dmg] [--publish] [--yes] [--dry-run] [--no-open-finder] [--parallel N]"
    echo ""
    echo "Sin flags y con terminal, pregunta por cada paso."
    echo ""
    echo "  --zip             Crear deploy/${ARTIFACT_NAME}_Mac_v<version>.zip (actualizacion)"
    echo "  --dmg             Crear deploy/${ARTIFACT_NAME}_Mac_v<version>.dmg (primera instalacion)"
    echo "  --publish         Crear el tag v<version> y el release en $RELEASE_REPO con esos assets"
    echo "  --yes             No pedir la confirmacion final antes de publicar"
    echo "  --dry-run         Todo menos lo remoto: no crea tags ni sube nada"
    echo "  --no-open-finder  No mostrar el resultado en Finder al terminar"
    echo "  --parallel N      Usar N nucleos para compilar"
}

ZIP_MODE="prompt"; DMG_MODE="prompt"; PUBLISH_MODE="prompt"
ASSUME_YES=false; DRY_RUN=false; NO_OPEN_FINDER=false
PARALLEL_CORES="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --zip) ZIP_MODE="always"; shift ;;
        --dmg) DMG_MODE="always"; shift ;;
        --publish) PUBLISH_MODE="always"; shift ;;
        --yes|-y) ASSUME_YES=true; shift ;;
        --dry-run) DRY_RUN=true; shift ;;
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
if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "ERROR: deploy.sh es solo para macOS."
    exit 1
fi

INTERACTIVE=false
[[ -t 0 ]] && INTERACTIVE=true

ask_yes_no() {
    # Respuesta por defecto: la que se pasa como segundo argumento (Y o N).
    local prompt="$1" def="$2" answer hint="[y/N]"
    [[ "$def" == "Y" ]] && hint="[Y/n]"
    read -r -p "$prompt $hint " answer
    answer="${answer:-$def}"
    [[ "$answer" =~ ^[Yy]$ ]]
}

decide() {
    # always -> si; prompt con terminal -> pregunta; prompt sin terminal -> no.
    local mode="$1" prompt="$2" def="$3"
    if [[ "$mode" == "always" ]]; then return 0; fi
    if [[ "$INTERACTIVE" == "true" ]]; then ask_yes_no "$prompt" "$def"; return $?; fi
    return 1
}

# ---- Version: una sola fuente (CMakeLists.txt), y el ChangeLog tiene que coincidir ----
APP_VERSION="$(sed -n 's/^[[:space:]]*project(.*VERSION[[:space:]]\{1,\}\([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
if ! [[ "$APP_VERSION" =~ ^[0-9]+(\.[0-9]+)+$ ]]; then
    echo "ERROR: no se pudo leer la version de CMakeLists.txt (project(... VERSION ...))."
    exit 1
fi
CHANGELOG_VERSION="$(sed -n 's/^v\([0-9][0-9.]*\):.*/\1/p' docs/ChangeLog.md | head -1)"
if [[ "$CHANGELOG_VERSION" != "$APP_VERSION" ]]; then
    echo "ERROR: CMakeLists.txt dice v$APP_VERSION pero la ultima entrada del ChangeLog es v${CHANGELOG_VERSION:-?}."
    exit 1
fi

TAG="v${APP_VERSION}"
ZIP_NAME="${ARTIFACT_NAME}_Mac_v${APP_VERSION}.zip"
DMG_NAME="${ARTIFACT_NAME}_Mac_v${APP_VERSION}.dmg"
APP_PATH="deploy/${APP_NAME}.app"

echo "Deploy de $APP_NAME v$APP_VERSION"
echo ""

# ---- Preguntas, todas juntas al principio ----
CREATE_ZIP=false; CREATE_DMG=false; PUBLISH=false
decide "$ZIP_MODE" "Crear el ZIP (actualizacion)?" "Y" && CREATE_ZIP=true
decide "$DMG_MODE" "Crear el DMG (primera instalacion)?" "Y" && CREATE_DMG=true
if [[ "$CREATE_ZIP" == "true" || "$CREATE_DMG" == "true" ]]; then
    decide "$PUBLISH_MODE" "Publicar el release $TAG en GitHub ($RELEASE_REPO) con esos archivos?" "N" && PUBLISH=true
elif [[ "$PUBLISH_MODE" == "always" ]]; then
    echo "ERROR: --publish necesita al menos --zip o --dmg."
    exit 1
fi

# ---- Chequeos de publicacion ANTES de compilar: que no se descubra al final ----
if [[ "$PUBLISH" == "true" ]]; then
    for cmd in git gh; do
        command -v "$cmd" >/dev/null 2>&1 || { echo "ERROR: falta el comando $cmd."; exit 1; }
    done
    if ! gh auth status >/dev/null 2>&1; then
        echo "ERROR: gh no esta autenticado. Correr 'gh auth login' con tu usuario."
        exit 1
    fi
    # El release lo crea la cuenta activa de gh, no la de git.
    GH_USER="$(gh api user -q .login 2>/dev/null || true)"
    if [[ "$GH_USER" != "legandrop" ]]; then
        echo "ERROR: la cuenta activa de gh es '${GH_USER:-?}', no legandrop. 'gh auth switch' y reintentar."
        exit 1
    fi
    VISIBILITY="$(gh repo view "$RELEASE_REPO" --json visibility -q .visibility 2>/dev/null || true)"
    if [[ "$VISIBILITY" != "PUBLIC" ]]; then
        echo "ERROR: $RELEASE_REPO no existe, no es accesible o no es publico."
        exit 1
    fi
    # GitHub atribuye el tag por email: con una cuenta vieja queda fuera del grafico de
    # contribuciones de la buena (ver reglas del repo, "GitHub, autoria y menciones").
    GIT_EMAIL="$(git config user.email || true)"
    if [[ "$GIT_EMAIL" != "176236735+legandrop@users.noreply.github.com" ]]; then
        echo "ERROR: git user.email es '${GIT_EMAIL:-?}', no la cuenta legandrop (176236735)."
        exit 1
    fi
    # El tag apunta a HEAD: con cambios sin commitear, el release no seria lo empaquetado.
    if [[ -n "$(git status --porcelain)" && "$DRY_RUN" != "true" ]]; then
        echo "ERROR: hay cambios sin commitear. Commitear y pushear antes de publicar."
        git status --short
        exit 1
    fi
    BRANCH="$(git rev-parse --abbrev-ref HEAD)"
    if [[ "$BRANCH" != "main" ]]; then
        echo "ERROR: hay que publicar desde main (branch actual: $BRANCH)."
        exit 1
    fi
    git fetch -q origin main
    if [[ "$(git rev-parse HEAD)" != "$(git rev-parse origin/main)" && "$DRY_RUN" != "true" ]]; then
        echo "ERROR: HEAD no coincide con origin/main. Pushear (o pullear) antes de publicar."
        exit 1
    fi
    if gh release view "$TAG" --repo "$RELEASE_REPO" >/dev/null 2>&1; then
        echo "AVISO: el release $TAG ya existe en $RELEASE_REPO; se van a reemplazar sus assets."
    fi
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

# Firma del bundle YA armado (la firma cubre el contenido). No es notarizacion: el usuario
# sigue necesitando el `xattr -cr` del Read Me del DMG. Con identidad estable si existe en el
# llavero; si no, ad-hoc.
CODESIGN_IDENTITY="${LGA_CODESIGN_IDENTITY:-LGA Code Signing}"
if security find-identity -v -p codesigning 2>/dev/null | grep -q "\"$CODESIGN_IDENTITY\""; then
    echo "Firmando con '$CODESIGN_IDENTITY'..."
    codesign --force --deep --sign "$CODESIGN_IDENTITY" "$APP_PATH"
else
    echo "Firmando ad-hoc (no hay identidad '$CODESIGN_IDENTITY' en el llavero)..."
    codesign --force --deep --sign - "$APP_PATH"
fi
echo "Bundle listo: $APP_PATH"

ASSETS=()
if [[ "$CREATE_ZIP" == "true" ]]; then
    # ditto y NO zip: `zip -r` resuelve los symlinks de los frameworks de Qt, duplica cada
    # uno y rompe la firma.
    rm -f "deploy/${ZIP_NAME}"
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
    ASSETS+=("deploy/${ZIP_NAME}")
fi
if [[ "$CREATE_DMG" == "true" ]]; then
    bash ./create_dmg.sh --no-open
    ASSETS+=("deploy/${DMG_NAME}")
fi

# ---- Publicacion ----
if [[ "$PUBLISH" == "true" ]]; then
    # Las notas nombran lo que de verdad se sube: con solo el ZIP no se manda a bajar un DMG.
    NOTES_FILE="$(mktemp "${TMPDIR:-/tmp}/${ARTIFACT_NAME}_notes_XXXXXX")"
    {
        echo "${APP_NAME} ${TAG} for macOS 12 or later (Apple Silicon and Intel)."
        echo ""
        if [[ "$CREATE_DMG" == "true" ]]; then
            echo "**Install:** download \`${DMG_NAME}\`, open it and drag the app onto Applications."
        else
            echo "**Install:** download \`${ZIP_NAME}\`, unzip it and move the app to Applications."
        fi
        echo "The app is not notarized by Apple: if macOS says it can't be opened, run"
        echo "\`sudo xattr -cr \"/Applications/${APP_NAME}.app\"\` in Terminal and open it again."
        [[ "$CREATE_DMG" == "true" ]] && echo "The DMG includes a Read Me with the same steps."
        echo ""
        echo "See the [README](https://github.com/${RELEASE_REPO}#readme) for what the app does."
    } > "$NOTES_FILE"

    echo ""
    echo "Listo para publicar:"
    echo "  repo:   $RELEASE_REPO"
    echo "  tag:    $TAG -> $(git rev-parse --short HEAD) ($(git log -1 --format=%s))"
    for a in "${ASSETS[@]}"; do echo "  asset:  $a ($(du -h "$a" | cut -f1))"; done
    echo "  notas:"
    sed 's/^/          /' "$NOTES_FILE"
    echo ""

    GO=false
    if [[ "$ASSUME_YES" == "true" ]]; then
        GO=true
    elif [[ "$INTERACTIVE" == "true" ]]; then
        ask_yes_no "Publicar ahora? Es publico y queda a la vista de todos." "N" && GO=true
    else
        echo "Sin terminal y sin --yes: no se publica."
    fi

    if [[ "$GO" == "true" ]]; then
        run() {
            if [[ "$DRY_RUN" == "true" ]]; then echo "[DRY RUN] $*"; else "$@"; fi
        }
        HEAD_COMMIT="$(git rev-parse HEAD)"
        LOCAL_TAG="$(git rev-list -n 1 "$TAG" 2>/dev/null || true)"
        REMOTE_TAG="$(git ls-remote origin "refs/tags/${TAG}^{}" | awk 'NR==1{print $1}')"
        [[ -n "$REMOTE_TAG" ]] || REMOTE_TAG="$(git ls-remote origin "refs/tags/${TAG}" | awk 'NR==1{print $1}')"
        if [[ -n "$LOCAL_TAG" && "$LOCAL_TAG" != "$HEAD_COMMIT" ]] || [[ -n "$REMOTE_TAG" && "$REMOTE_TAG" != "$HEAD_COMMIT" ]]; then
            echo "ERROR: el tag $TAG ya existe y apunta a otro commit. No se mueve solo: revisarlo."
            exit 1
        fi
        [[ -n "$LOCAL_TAG" ]] || run git tag -a "$TAG" -m "Release $TAG"
        [[ -n "$REMOTE_TAG" ]] || run git push origin "$TAG"

        if gh release view "$TAG" --repo "$RELEASE_REPO" >/dev/null 2>&1; then
            run gh release upload "$TAG" "${ASSETS[@]}" --repo "$RELEASE_REPO" --clobber
        else
            run gh release create "$TAG" "${ASSETS[@]}" --repo "$RELEASE_REPO" \
                --verify-tag --title "${APP_NAME} ${TAG}" --notes-file "$NOTES_FILE"
        fi
        rm -f "$NOTES_FILE"

        # El manifiesto de LGA Updates (que lee el sitio para el boton Descargar) se refresca
        # por cron; esto solo lo adelanta.
        if [[ "$DRY_RUN" != "true" ]]; then
            if gh workflow run refresh_versions.yml --repo legandrop/LGA_Updates >/dev/null 2>&1 </dev/null; then
                echo "Manifiesto de LGA Updates: refresco disparado."
            else
                echo "AVISO: no se pudo disparar el refresco del manifiesto; el cron lo levanta solo."
            fi
        fi
        echo ""
        echo "Release: https://github.com/${RELEASE_REPO}/releases/tag/${TAG}"
    else
        rm -f "$NOTES_FILE"
        echo "No se publico nada. Los archivos quedan en deploy/."
    fi
fi

if [[ "$NO_OPEN_FINDER" == "false" && "$INTERACTIVE" == "true" ]]; then
    ask_yes_no "Mostrar deploy/ en Finder?" "N" && open "deploy"
fi
echo "Deploy terminado."
