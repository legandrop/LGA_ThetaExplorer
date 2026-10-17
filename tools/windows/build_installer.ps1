#Requires -Version 5.1
param([switch]$LocalOnly, [switch]$NoRun, [switch]$StageOnly, [switch]$Publish)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$utf8 = [Text.UTF8Encoding]::new($false)
$repo = 'legandrop/LGA_ThetaExplorer'

function Run-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command fallo con codigo $LASTEXITCODE" }
}
function Confirm-Choice([string]$Message) {
    & "$env:SystemRoot\System32\choice.exe" /C YN /M $Message
    if ($LASTEXITCODE -notin 1, 2) { throw 'No se pudo leer la respuesta Y/N' }
}
function Git-Value([string[]]$Arguments) {
    $value = & git @Arguments
    if ($LASTEXITCODE -ne 0) { throw "git $Arguments fallo" }
    return ($value -join "`n").Trim()
}
function File-Sha256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-','').ToLowerInvariant() }
    finally { $stream.Dispose(); $algorithm.Dispose() }
}
function Read-Version {
    $match = [regex]::Match([IO.File]::ReadAllText((Join-Path $root 'CMakeLists.txt')), 'project\(ThetaExplorer VERSION (\d+\.\d{3})\b')
    if (-not $match.Success) { throw 'No se pudo leer la version de CMake' }
    return $match.Groups[1].Value
}
# Lo que impide publicar cualquier version. Solo lee y no corta: HEAD en origin, el tag y el
# release dependen de la version que se termine publicando y los mira Publication-Preflight.
function Publish-Blockers {
    $ErrorActionPreference = 'Continue'
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { 'Falta GitHub CLI para publicar' }
    else {
        $account = & gh api user --jq .login 2>$null
        if ($LASTEXITCODE -ne 0 -or $account -ne 'legandrop') { 'GitHub debe estar autenticado como legandrop' }
        $visibility = & gh repo view $repo --json visibility --jq .visibility 2>$null
        if ($LASTEXITCODE -ne 0 -or $visibility -ne 'PUBLIC') { 'No se pudo verificar el repo publico' }
    }
    if ((Git-Value @('config','user.email')) -ne '176236735+legandrop@users.noreply.github.com') { 'Identidad Git incorrecta' }
    if ((Git-Value @('remote','get-url','origin')) -notmatch '^(https://github\.com/|git@github\.com:)legandrop/LGA_ThetaExplorer(?:\.git)?$') { 'Remoto incorrecto' }
    if ((Git-Value @('branch','--show-current')) -ne 'main') { 'Publicar requiere main' }
    if (Git-Value @('status','--porcelain')) { 'Publicar requiere el arbol limpio' }
}
# Estado del release v<version> para Windows: none (no existe), other (existe sin el setup de
# Windows, lo creo la Mac), mine (ya lo tiene), draft o error. Solo lee.
function Release-State {
    $ErrorActionPreference = 'Continue'
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { return 'error' }
    $out = @(& gh release view "v$version" --repo $repo --json isDraft,assets --jq '.isDraft, .assets[].name' 2>&1 | ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 0) { if ($out -match 'not found') { return 'none' }; return 'error' }
    if ($out[0] -eq 'true') { return 'draft' }
    if ($out -contains "LGA_ThetaExplorer_Setup_v$version.exe") { return 'mine' }
    return 'other'
}
# Una tecla, sin Enter; una que no es opcion no hace nada. choice lee tambien un pipe; EOF o
# Ctrl+C cuentan como cancelar.
function Read-Option([string]$Keys) {
    & "$env:SystemRoot\System32\choice.exe" /C $Keys /N /M 'Opcion: ' | Out-Host
    if ($LASTEXITCODE -lt 1 -or $LASTEXITCODE -gt $Keys.Length) { return '3' }
    return $Keys[$LASTEXITCODE - 1].ToString()
}
function Local-Or-Cancel {
    Write-Host '  2. Instalador local, sin subir a GitHub'
    Write-Host '  3. Cancelar'
    return (Read-Option '23')
}
function Publication-Preflight {
    $blockers = @(Publish-Blockers)
    if ($blockers.Count) { throw ($blockers -join '; ') }
    $commit = Git-Value @('rev-parse','HEAD')
    $remote = (Git-Value @('ls-remote','origin','refs/heads/main')) -split '\s+'
    if ($remote[0] -ne $commit) { throw 'Publicar requiere HEAD ya pusheado a origin/main' }
    $refs = Git-Value @('ls-remote','origin',"refs/tags/v$version", "refs/tags/v$version^{}")
    if ($refs) {
        $tagLines = $refs -split "`n"
        $peeled = $tagLines | Where-Object { $_ -match '\^\{\}$' }
        $target = if ($peeled) { ($peeled -split '\s+')[0] } else { ($tagLines[0] -split '\s+')[0] }
        if ($target -ne $commit) { throw 'El tag ya existe: no se reemplaza una version publicada' }
    }
    $releases = & gh api "repos/$repo/releases" --paginate --jq '.[].tag_name'
    if ($LASTEXITCODE -ne 0) { throw 'No se pudieron comprobar los releases' }
    # El release es uno solo para Windows y macOS: si la Mac ya lo creo (con el tag en este mismo
    # commit, chequeado arriba), Windows le suma su instalador. Lo que no se hace es reemplazar un
    # instalador de Windows ya publicado.
    $script:releaseExists = $releases -contains "v$version"
    if ($script:releaseExists) {
        $assets = & gh release view "v$version" --repo $repo --json assets --jq '.assets[].name'
        if ($LASTEXITCODE -ne 0) { throw 'No se pudieron leer los archivos del release existente' }
        if ($assets -contains "LGA_ThetaExplorer_Setup_v$version.exe") { throw 'El release ya tiene el instalador de Windows: no se reemplaza' }
    }
    return $commit
}

try {
    Set-Location -LiteralPath $root
    if ($Publish -and ($LocalOnly -or $StageOnly -or $NoRun)) {
        throw '--publish no admite --local, --stage-only ni --no-run'
    }
    $version = Read-Version
    # CMakeLists.txt guarda la ultima version PUBLICADA y el ChangeLog la EN DESARROLLO. La
    # publicacion se decide ACA, antes de compilar (el numero queda adentro del binario), con el
    # mismo criterio que publish_release.sh --decide en la Mac: se mira el release v<version> y se
    # pregunta UNA vez, con una tecla. 1 es publicar v<version> (release nuevo), sumar el setup al
    # release que ya creo la Mac o, si el changelog va adelante, el bump a esa version (commit +
    # push) y publicarla; 2 instalador local; 3 cancelar. Con la version ya publicada para Windows y
    # sin version nueva en el changelog no hay nada que publicar: republicar el mismo numero deja
    # sin update a quien ya lo tiene. --publish no pregunta: publica si el estado lo permite.
    $interactive = -not $NoRun -and -not [Console]::IsInputRedirected
    $publishNow = $false
    $key = '2'
    if ($Publish) { $key = '1' }
    elseif (-not $LocalOnly -and -not $StageOnly -and -not $NoRun) {
        $devMatch = [regex]::Match([IO.File]::ReadAllText((Join-Path $root 'docs\ChangeLog.md')), '(?m)^v(\d+\.\d{3})\s*:')
        $devVersion = $devMatch.Groups[1].Value
        $ahead = $devMatch.Success -and ([version]$devVersion -gt [version]$version)
        $state = Release-State
        try { $blockers = @(Publish-Blockers) } catch { $blockers = @("$_") }
        Write-Host ''
        Write-Host '============================================================'
        Write-Host ' Publicacion en GitHub [Theta Explorer, Windows]'
        Write-Host '============================================================'
        switch ($state) {
            'none' { Write-Host "v$version no esta publicada en GitHub." }
            'other' { Write-Host "v$version esta en GitHub, sin el instalador de Windows." }
            'mine' { Write-Host "v$version ya esta publicada en GitHub con el instalador de Windows." }
            'draft' { $blockers += "el release v$version es un borrador (publicarlo o borrarlo a mano)" }
            default { $blockers += "no se pudo leer el release v$version" }
        }
        if ($ahead) { Write-Host "El changelog tiene v$devVersion en desarrollo." }
        $action = ''
        if ($ahead) {
            if (Test-Path -LiteralPath (Join-Path $root 'tools\bump_version.ps1')) {
                $action = 'bump'; $label = "Bump a v$devVersion (commit + push) y publicarla"
            } else { Write-Host "Para publicar v$devVersion falta el bump (no esta tools\bump_version.ps1)." }
        } elseif ($state -eq 'mine') { Write-Host 'Para publicar, primero la version nueva en el changelog.' }
        elseif ($state -eq 'none') { $action = 'publish'; $label = "Publicar v$version como release nuevo" }
        elseif ($state -eq 'other') { $action = 'publish'; $label = "Sumar el instalador de Windows al release v$version" }
        if ($blockers.Count) {
            $action = ''
            Write-Host 'No se puede publicar:'
            $blockers | ForEach-Object { Write-Host "  - $_" }
        }
        Write-Host ''
        if ($action) {
            Write-Host "  1. $label"
            Write-Host '  2. Instalador local, sin subir a GitHub'
            Write-Host '  3. Cancelar'
            $key = Read-Option '123'
        } else { $key = Local-Or-Cancel }
        if ($key -eq '1' -and $action -eq 'bump') {
            Write-Host ''
            Write-Host "Bump a v$devVersion (commit + push)..."
            & (Join-Path $root 'tools\bump_version.ps1') -Commit -NoPublish
            if ($LASTEXITCODE -ne 0) {
                Write-Host 'El bump fallo (ver arriba).'
                $key = Local-Or-Cancel
            }
            $version = Read-Version
        }
    }
    if ($key -eq '1') {
        # Lo elegido se verifica entero: el bump commiteado, HEAD en origin, el tag y el release.
        try {
            & (Join-Path $root 'tools\bump_version.ps1') -Check
            if ($LASTEXITCODE -ne 0) { throw 'Falta el bump de version: correr bump_version.bat' }
            $commit = Publication-Preflight
            $publishNow = $true
        } catch {
            if ($Publish) { throw }
            Write-Host "No se puede publicar: $_"
            $key = Local-Or-Cancel
        }
    }
    if ($key -eq '3') { Write-Host 'Cancelado. No se compilo nada.'; exit 1 }
    if (-not $publishNow -and -not $LocalOnly -and -not $StageOnly -and -not $NoRun) {
        Write-Host "Se genera el instalador local de v$version, sin publicar."
    }
    $compiler = if ($env:THETA_ISCC) { $env:THETA_ISCC } else { Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe' }
    if (-not $StageOnly -and -not (Test-Path -LiteralPath $compiler -PathType Leaf)) { throw 'Falta Inno Setup 6; indicar ISCC.exe con THETA_ISCC' }
    Run-Checked (Join-Path $root 'compilar.bat') @('--release','--no-run')

    # Lista cerrada: no incluir cache, CMake, fuentes, logs ni simbolos del build.
    $files = @('LGA_ThetaExplorer.exe','libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll',
        'Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6Concurrent.dll','Qt6Network.dll','Qt6Svg.dll',
        'platforms\qwindows.dll','platforms\qoffscreen.dll','imageformats\qjpeg.dll',
        'imageformats\qico.dll','imageformats\qsvg.dll','iconengines\qsvgicon.dll','tls\qschannelbackend.dll')
    $build = Join-Path $root 'build-release'
    foreach ($rel in @('tls\qcertonlybackend.dll','networkinformation\qnetworklistmanager.dll')) {
        if (Test-Path -LiteralPath (Join-Path $build $rel)) { $files += $rel }
    }
    $output = Join-Path $root "deploy\windows-v$version"
    $package = Join-Path $output 'runtime'
    New-Item -ItemType Directory -Path $package -Force | Out-Null
    # No barrer una carpeta anterior. Un resto desconocido corta el empaquetado.
    foreach ($existing in (Get-ChildItem -LiteralPath $package -Recurse -File)) {
        $rel = $existing.FullName.Substring($package.Length + 1)
        if ($rel -notin $files) { throw "Archivo ajeno al paquete: $($existing.FullName)" }
    }
    foreach ($rel in $files) {
        $target = Join-Path $package $rel
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $build $rel) -Destination $target -Force
    }
    $mingw = if ($env:THETA_MINGW_BIN) { $env:THETA_MINGW_BIN } else { 'C:\Qt\Tools\mingw1310_64\bin' }
    Run-Checked (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',
        (Join-Path $PSScriptRoot 'check_deploy_runtime.ps1'), '-DeployDir',$package,
        '-Objdump',(Join-Path $mingw 'objdump.exe'),'-PluginDirs','platforms,imageformats,iconengines,tls,networkinformation',
        '-Require',($files -join ','))
    $lines = @(
        '; Archivo generado por instalador.bat; editar tools/windows/build_installer.ps1.',
        '[Setup]', 'AppId=LGA_ThetaExplorer', 'AppName=Theta Explorer', "AppVersion=$version",
        'AppPublisher=Lega Pugliese', 'AppPublisherURL=https://lega.com.ar',
        'DefaultDirName=C:\Portable\LGA\ThetaExplorer', 'DefaultGroupName=Theta Explorer',
        'PrivilegesRequired=lowest', 'ArchitecturesAllowed=x64compatible',
        'ArchitecturesInstallIn64BitMode=x64compatible', 'MinVersion=10.0',
        'UsePreviousAppDir=yes', 'DirExistsWarning=no', 'Compression=lzma2', 'SolidCompression=yes',
        'CloseApplications=no', 'RestartApplications=no',
        'UninstallDisplayIcon={app}\LGA_ThetaExplorer.exe',
        "SetupIconFile=$root\resources\icons\LGA_ThetaExplorer.ico", "OutputDir=$output",
        "OutputBaseFilename=LGA_ThetaExplorer_Setup_v$version", '', '[Files]')
    $manifest = foreach ($rel in $files) {
        $source = Join-Path $package $rel
        $destination = Split-Path $rel -Parent
        $destDir = if ($destination) { "{app}\$destination" } else { '{app}' }
        # La app no autoactualiza sus DLL: deben reemplazarse en cada instalacion.
        $lines += "Source: `"$source`"; DestDir: `"$destDir`"; Flags: ignoreversion"
        @{ path = $rel; bytes = (Get-Item -LiteralPath $source).Length; sha256 = (File-Sha256 $source) }
    }
    $lines += @("Source: `"$PSScriptRoot\close_by_path.ps1`"; Flags: dontcopy",
        "Source: `"$PSScriptRoot\close_by_path.ps1`"; DestDir: `"{app}\tools`"; Flags: ignoreversion", '',
        '[Tasks]', 'Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"', '',
        '[Icons]', 'Name: "{group}\Theta Explorer"; Filename: "{app}\LGA_ThetaExplorer.exe"',
        'Name: "{userdesktop}\Theta Explorer"; Filename: "{app}\LGA_ThetaExplorer.exe"; Tasks: desktopicon', '',
        '[Run]', 'Filename: "{app}\LGA_ThetaExplorer.exe"; Description: "Launch Theta Explorer"; Flags: nowait postinstall', '',
        '[UninstallDelete]', 'Type: filesandordirs; Name: "{app}\cache"', '', '[Code]')
    # Solo cache propia. Settings/historial y el destino de fotos se conservan.
    $code = @'
function CloseTheta(const ScriptPath: String): String;
var
  ResultCode: Integer;
  Params: String;
begin
  Result := '';
  Params := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File ' + #34 + ScriptPath + #34 + ' -ExeName LGA_ThetaExplorer.exe -AllInstances -Tree';
  if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Result := 'Could not close Theta Explorer. Close all copies and try again.'
  else if ResultCode <> 0 then
    Result := 'Could not close Theta Explorer. Close all copies and try again.';
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  ExtractTemporaryFile('close_by_path.ps1');
  Result := CloseTheta(ExpandConstant('{tmp}\close_by_path.ps1'));
end;

function InitializeUninstall: Boolean;
var
  Error: String;
begin
  Error := CloseTheta(ExpandConstant('{app}\tools\close_by_path.ps1'));
  Result := Error = '';
  if not Result then
    SuppressibleMsgBox(Error, mbError, MB_OK, IDOK);
end;
'@
    $lines += $code
    $iss = Join-Path $output 'ThetaExplorer.iss'
    [IO.File]::WriteAllText($iss, ($lines -join "`r`n"), [Text.UTF8Encoding]::new($true))
    [IO.File]::WriteAllText((Join-Path $output 'package-manifest.json'), (ConvertTo-Json -InputObject @($manifest) -Depth 4), $utf8)
    if ($StageOnly) { Write-Host "Paquete preparado: $iss"; exit 0 }
    Run-Checked $compiler @('/Qp',$iss)
    $setup = Join-Path $output "LGA_ThetaExplorer_Setup_v$version.exe"
    if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) { throw 'No se genero el instalador' }
    $hashFile = Join-Path $output 'SHA256SUMS'
    [IO.File]::WriteAllText($hashFile, ((File-Sha256 $setup) + '  ' + [IO.Path]::GetFileName($setup) + "`n"), $utf8)
    Write-Host "Instalador listo: $setup"
    if ($interactive) {
        Confirm-Choice 'Desea ejecutar el instalador ahora mismo?'
        if ($LASTEXITCODE -eq 1) {
            Start-Process -FilePath $setup -Wait
        } else {
            Write-Host 'Instalador no ejecutado.'
        }
        Confirm-Choice 'Desea revelar el instalador en Windows Explorer?'
        if ($LASTEXITCODE -eq 1) {
            if (Test-Path -LiteralPath $setup) { & explorer.exe /select, $setup }
        } else {
            Write-Host 'No se abrio Windows Explorer.'
        }
    }
    if ($publishNow) {
        # La decision ya se tomo antes de compilar: aca no se vuelve a preguntar.
        $currentCommit = Publication-Preflight
        if ($currentCommit -ne $commit) { throw 'El commit cambio durante el empaquetado' }
        Write-Host "Release publico: $repo / v$version / $commit"
        Write-Host "Archivos: $setup y $hashFile"
        if ($script:releaseExists) {
            Run-Checked 'gh' @('release','upload',"v$version",$setup,$hashFile,'--repo',$repo)
        } else {
            # Titulo y notas de tools/macos/release.conf, los mismos que usa publish_release.sh en la
            # Mac: el release es uno solo y no cambia segun que plataforma lo cree.
            $conf = [IO.File]::ReadAllText((Join-Path $root 'tools\macos\release.conf'))
            $title = [regex]::Match($conf, '(?m)^RELEASE_TITLE="([^"]*)"').Groups[1].Value
            $body = [regex]::Match($conf, "(?ms)^RELEASE_NOTES='((?:[^']|'`"'`"')*)'").Groups[1].Value
            if (-not $title -or -not $body) { throw 'No se pudieron leer RELEASE_TITLE y RELEASE_NOTES de tools/macos/release.conf' }
            $notes = Join-Path $output 'release-notes.md'
            [IO.File]::WriteAllText($notes, $body.Replace("'`"'`"'", "'").Replace('{v}', $version), $utf8)
            Run-Checked 'gh' @('release','create',"v$version",$setup,$hashFile,'--repo',$repo,'--target',$commit,
                '--title',$title.Replace('{v}', $version),'--notes-file',$notes,'--latest')
        }
    }
    exit 0
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
