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
    & "$env:SystemRoot\System32\choice.exe" /C YN /M $Message | Out-Host
    if ($LASTEXITCODE -notin 1, 2) { throw 'No se pudo leer la respuesta Y/N' }
    return $LASTEXITCODE -eq 1
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
function Publication-Preflight {
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw 'Falta GitHub CLI para publicar' }
    if ((Git-Value @('config','user.email')) -ne '176236735+legandrop@users.noreply.github.com') { throw 'Identidad Git incorrecta' }
    if ((Git-Value @('remote','get-url','origin')) -notmatch '^(https://github\.com/|git@github\.com:)legandrop/LGA_ThetaExplorer(?:\.git)?$') { throw 'Remoto incorrecto' }
    if ((Git-Value @('branch','--show-current')) -ne 'main') { throw 'Publicar requiere main' }
    if (Git-Value @('status','--porcelain')) { throw 'Publicar requiere el arbol limpio' }
    $commit = Git-Value @('rev-parse','HEAD')
    $remote = (Git-Value @('ls-remote','origin','refs/heads/main')) -split '\s+'
    if ($remote[0] -ne $commit) { throw 'Publicar requiere HEAD ya pusheado a origin/main' }
    $account = & gh api user --jq .login
    if ($LASTEXITCODE -ne 0 -or $account -ne 'legandrop') { throw 'GitHub debe estar autenticado como legandrop' }
    $visibility = & gh repo view $repo --json visibility --jq .visibility
    if ($LASTEXITCODE -ne 0 -or $visibility -ne 'PUBLIC') { throw 'No se pudo verificar el repo publico' }
    $refs = Git-Value @('ls-remote','origin',"refs/tags/v$version", "refs/tags/v$version^{}")
    if ($refs) {
        $tagLines = $refs -split "`n"
        $peeled = $tagLines | Where-Object { $_ -match '\^\{\}$' }
        $target = if ($peeled) { ($peeled -split '\s+')[0] } else { ($tagLines[0] -split '\s+')[0] }
        if ($target -ne $commit) { throw 'El tag ya existe: no se reemplaza una version publicada' }
    }
    $releases = & gh api "repos/$repo/releases" --paginate --jq '.[].tag_name'
    if ($LASTEXITCODE -ne 0) { throw 'No se pudieron comprobar los releases' }
    if ($releases -contains "v$version") { throw 'El release ya existe: no se reemplazan sus archivos' }
    return $commit
}

try {
    Set-Location -LiteralPath $root
    if ($Publish -and ($LocalOnly -or $StageOnly -or $NoRun -or [Console]::IsInputRedirected)) {
        throw '--publish requiere consola interactiva y no admite --local, --stage-only ni --no-run'
    }
    $match = [regex]::Match([IO.File]::ReadAllText((Join-Path $root 'CMakeLists.txt')), 'project\(ThetaExplorer VERSION (\d+\.\d{3})\b')
    if (-not $match.Success) { throw 'No se pudo leer la version de CMake' }
    $version = $match.Groups[1].Value
    $changelog = [IO.File]::ReadAllText((Join-Path $root 'docs\ChangeLog.md'))
    if ([regex]::Match($changelog, '(?m)^v(\d+\.\d{3}):').Groups[1].Value -ne $version) { throw 'CMake y ChangeLog no coinciden' }
    $interactive = -not $NoRun -and -not [Console]::IsInputRedirected
    $offerPublication = -not $LocalOnly -and -not $StageOnly -and $interactive
    if ($offerPublication) {
        try { $commit = Publication-Preflight }
        catch {
            if ($Publish) { throw }
            Write-Warning "No se puede publicar: $_. Se genera el instalador localmente."
            $offerPublication = $false
        }
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
        if (Confirm-Choice 'Desea ejecutar el instalador ahora mismo (instalar local)?') {
            Start-Process -FilePath $setup -Wait
        } else {
            Write-Host 'Instalador no ejecutado.'
        }
        if (Confirm-Choice 'Desea revelar el instalador en Windows Explorer?') {
            if (Test-Path -LiteralPath $setup) { & explorer.exe /select, $setup }
        } else {
            Write-Host 'No se abrio Windows Explorer.'
        }
    }
    if ($offerPublication) {
        $currentCommit = Publication-Preflight
        if ($currentCommit -ne $commit) { throw 'El commit cambio durante el empaquetado' }
        Write-Host "Release publico: $repo / v$version / $commit"
        Write-Host "Archivos: $setup y $hashFile"
        if (Confirm-Choice "Desea subir el instalador como release v$version a GitHub?") {
            $notes = Join-Path $output 'release-notes.md'
            [IO.File]::WriteAllText($notes, 'First Windows release. Browse a RICOH THETA over USB, preview photos, group HDR brackets and download JPG/DNG files. Windows 10/11 x64; no Ricoh software required.', $utf8)
            Run-Checked 'gh' @('release','create',"v$version",$setup,$hashFile,'--repo',$repo,'--target',$commit,
                '--title',"Theta Explorer v$version",'--notes-file',$notes,'--latest')
        } else {
            Write-Host 'Release no publicado.'
        }
    }
    exit 0
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
