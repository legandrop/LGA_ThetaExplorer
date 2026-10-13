# Deploy y release

La app y su ejecutable se llaman `LGA ThetaExplorer`; el nombre corto del menu sigue
siendo `ThetaExplorer`. Los archivos ZIP y DMG usan `LGA_ThetaExplorer_Mac_v<version>`.

`./deploy.sh` hace todo en un solo paso. Con terminal pregunta al principio que armar y si
publicar, y despues corre sin frenar hasta la confirmacion final de la publicacion.

## Compilacion local y acceso a la camara

`compilar.sh` es el unico motor de compilacion: Debug incremental en `build/` por defecto,
Release en `build-release/` con `--release`. Lanza la app en background; `--no-run` compila
sin lanzar y `--wait` espera hasta que se cierre. Sustituye al antiguo `compilar_dev.sh`.

Tambien firma el bundle de Debug o Release despues de copiar los recursos y
dependencias, incluso si el build es incremental o usa `--no-run`. Usa `LGA Code Signing`
(override `LGA_CODESIGN_IDENTITY`) si existe en el llavero; si no, firma ad-hoc. Verifica la
firma con `codesign --verify --deep --strict` y detiene el script si no valida.

macOS comprueba la identidad de la app antes de conceder acceso a dispositivos extraibles,
incluida la THETA por USB. Un bundle sin firma puede quedar en «Searching for camera» sin
recibir el dispositivo: el servicio del sistema rechaza la conexion antes del callback.
La identidad estable evita que cada build cambie de identidad; el permiso lo concede macOS
al usuario, no el script. La firma local no instala la app ni publica un release.

## Que hace

1. **Version**: la lee de `CMakeLists.txt` (`project(... VERSION ...)`) y exige que la primera
   entrada de `docs/ChangeLog.md` diga lo mismo.
2. **Si se va a publicar, chequea antes de compilar**: `gh` autenticado, repo publico, git con
   la cuenta `legandrop` (176236735), working tree limpio, branch `main` y `HEAD` igual a
   `origin/main`. El tag apunta a `HEAD`, asi que con algo sin commitear o sin pushear el
   release no seria lo empaquetado.
3. **Build Release**: `./compilar.sh --release --no-run`, en `build-release/` (arbol
   separado: no toca la cache de desarrollo ni cierra la app de `build/`), universal
   arm64 + x86_64 y con `macdeployqt`.
4. **Bundle**: `ditto` a `deploy/LGA ThetaExplorer.app`, valida que los binarios sean todos
   universales (`tools/macos/validate_universal_macho.sh`) y firma. Usa la identidad estable
   `LGA Code Signing` si esta en el llavero (override: `LGA_CODESIGN_IDENTITY`); si no, ad-hoc.
   No hay notarizacion.
5. **ZIP** (`LGA_ThetaExplorer_Mac_v<version>.zip`): con `ditto`, nunca `zip` (resuelve
   los symlinks de los frameworks de Qt y rompe la firma). Se descomprime y se verifica que
   conserve symlinks y que la firma valide.
6. **DMG** (`LGA_ThetaExplorer_Mac_v<version>.dmg`): `create_dmg.sh`, copiado de
   LGA_Calmate. Usa `dmgbuild` vendorizado en `tools/macos/vendor` (Python puro, corre con el
   `python3` de macOS, no instala nada) y el fondo versionado `resources/dmg/dmg_background.tiff`.
   Adentro va un `Read Me.txt` en ingles con el `sudo xattr -cr` para abrir una app sin
   notarizar. Al final monta el DMG y verifica el `.DS_Store` (fondo, alias a Applications).
7. **Release**: muestra repo, tag, commit, assets y notas, y pide confirmacion (por defecto
   **no**). Crea el tag anotado `v<version>`, lo pushea y crea el release en
   `legandrop/LGA_ThetaExplorer` (el repo publico es el mismo del codigo). Si el release ya
   existe, reemplaza sus assets. Despues dispara el refresco del manifiesto de
   `legandrop/LGA_Updates`, que es de donde el sitio saca el boton Descargar.

## Flags

Sin terminal no pregunta nada y hace solo lo que digan los flags: `--zip`, `--dmg`,
`--publish` (necesita `--yes` para publicar sin terminal), `--dry-run` (todo menos lo
remoto), `--no-open-finder`, `--parallel N`.

## Fondo del DMG

No se regenera en cada deploy. Si cambia el nombre de la app:

```bash
osascript -l JavaScript tools/macos/make_dmg_background.js resources/dmg/dmg_background.tiff \
    "LGA ThetaExplorer" "Drag to install into your Applications folder" "LGA ThetaExplorer"
```

Las placas claras debajo de cada nombre se miden contra el texto: si cambia el nombre del
`Read Me.txt` en `create_dmg.sh`, cambiarlo tambien en el `.js`.

## Manifiesto de LGA Updates

El boton Descargar de la web de LGA sale de `versions.json` de `legandrop/LGA_Updates`. Para que
aparezca, `legandrop/LGA_ThetaExplorer` tiene que estar en `repos.json` de ese repo.
