# Deploy y release

## Version

`CMakeLists.txt` (`project(ThetaExplorer VERSION x.yyy ...)`) guarda la ultima version
PUBLICADA; el primer `vX.YYY:` de `docs/ChangeLog.md` es la version EN DESARROLLO, donde se
juntan las entradas hasta el proximo release. El numero sube solo con el bump, que corre Lega al
publicar y vale para Windows y macOS (los dos publican en el mismo release `v<version>`):

```bash
./bump_version.sh          # macOS; en Windows: bump_version.bat
```

Verifica que se este en `main`, al dia con origin, con el changelog y `CMakeLists.txt`
commiteados y con la identidad de `legandrop`; sube `CMakeLists.txt` a la version del changelog
(un salto mayor que `+0.001` se confirma); commitea solo ese archivo y pushea (lista antes los
commits locales que salen en el mismo push); y ofrece publicar con `./deploy.sh --zip --dmg
--publish` o `instalador.bat`. `--check` no escribe: sale 0 si `CMakeLists.txt` y el changelog
coinciden y el bump esta commiteado. `deploy.sh` e `instalador.bat` lo usan antes de publicar:
sin bump arman lo local con la version publicada y no ofrecen publicar (con `--publish`, cortan).
El release es uno solo y no importa que plataforma publique primero: la segunda le suma sus
archivos. Las dos tienen que publicar desde el commit del bump, porque el tag `v<version>` apunta
ahi y ninguna lo mueve.

## Windows

`instalador.bat` conserva la version actual de `CMakeLists.txt`; no la incrementa (eso es el
bump).
Requiere el toolchain de `compilar.bat` e Inno Setup 6 (se puede indicar su
`ISCC.exe` con `THETA_ISCC`). No instala herramientas globales automaticamente.

```bat
instalador_local.bat --no-run
```

Compila mediante `compilar.bat --release --no-run`, prepara el runtime de lista
cerrada y valida sus imports con `tools/windows/check_deploy_runtime.ps1`, copia
canonica del Base. Genera el `.iss`, un manifiesto de archivos, el setup
`LGA_ThetaExplorer_Setup_v<version>.exe` y `SHA256SUMS` en
`deploy/windows-v<version>/`. No incluye cache, fuentes, logs ni simbolos de build.
El `.iss` se regenera: se edita `tools/windows/build_installer.ps1`.
`--stage-only` prepara y verifica el paquete sin compilar el setup.

Sin flags y con consola interactiva, ofrece instalar, revelar el setup en Explorer
y publicar en GitHub, en ese orden y con preguntas `Y/N` independientes mediante
`choice /C YN` directo a la consola, igual que los otros instaladores. El modo local no requiere
Git/GitHub ni ofrece publicar. `--no-run`, `--stage-only` y entrada redirigida
omiten las preguntas y la publicacion.
El destino predeterminado es `C:\Portable\LGA\ThetaExplorer`; los ajustes e
historial siguen en AppData. Al desinstalar se retira la cache propia y se conservan
las fotos descargadas. El cierre por ruta usa el script canonico del Base y alcanza
tambien al auxiliar WPD, que comparte el ejecutable de la app.

Para publicar despues del bump (que ya commitea y pushea; `bump_version.bat` lo ofrece al
terminar):

```bat
instalador.bat
```

Requiere `main` limpio, HEAD publicado, identidad Git y GitHub `legandrop` y el
repo publico correcto. Rechaza un tag de otro commit. Si el release ya existe (lo creo la Mac),
le suma el setup y `SHA256SUMS`; si ya tiene el setup de Windows, no lo reemplaza.
Muestra repo, version, commit y archivos; solo crea el release si se responde `Y`
a la pregunta final. Sube el setup y `SHA256SUMS` al mismo repo publico y no
modifica permisos ni el manifiesto de LGA Updates. Si GitHub no esta listo,
avisa y genera el instalador localmente. `--publish` sigue disponible para exigir
esa verificacion antes de empaquetar, con la misma confirmacion `Y/N`; no admite
`--local`, `--stage-only`, `--no-run` ni entrada redirigida.

La validacion del empaquetador genera el setup y comprueba sus hashes e insumos;
no ejecuta el setup real ni modifica una instalacion del usuario. La instalacion
y desinstalacion reales se comprueban con el usuario mirando.

## macOS

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

1. **Version**: la lee de `CMakeLists.txt` (`project(... VERSION ...)`). Si el bump no esta
   hecho y commiteado (`./bump_version.sh --check`), arma lo local con esa version y no ofrece
   publicar; con `--publish` corta, y con `--dry-run` solo avisa.
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
