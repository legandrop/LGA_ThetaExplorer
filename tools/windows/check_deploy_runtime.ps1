# check_deploy_runtime.ps1 -- rev 1 (2026-10-07)
#
# La copia CANONICA vive en LGA_Base_QT_C_Py\tools\check_deploy_runtime.ps1. Cada app lleva en su
# propio tools\ una copia IDENTICA, esta cabecera incluida (solo pueden cambiar los fines de
# linea), y no la modifica: los arreglos se hacen en la canonica y se vuelven a copiar.
#
# Verifica que al paquete que se va a instalar no le falte runtime.
#
# Los scripts de deploy copian una lista de DLLs y plugins en vez de la instalacion entera de
# Qt y de thirdparty. El costo de eso es que una dependencia nueva que nadie agrego a la lista
# ya no llega "de casualidad", y el build de desarrollo -que se abastece solo- no lo nota: la
# app instalada es la unica que falla. Este script lo corta antes, en el deploy.
#
# Que chequea:
#   1. -Require: rutas [relativas al paquete] que tienen que existir. Es para lo que no se
#      puede deducir de una tabla de imports: los plugins de Qt, que se cargan en runtime.
#   2. -CompilarBat [opcional]: que cada entrada de la DEP_LIST de ese compilar.bat este en el
#      paquete. Las entradas con carpeta se buscan bajo la primera de -PluginDirs.
#   3. Que cada DLL que importa un binario del paquete este donde Windows la va a buscar:
#        - La app: los exe y dll de la raiz, mas las dll bajo -PluginDirs [recursivo]. Los
#          plugins de Qt los carga el exe de la raiz, asi que sus imports se resuelven
#          contra la RAIZ, no contra la carpeta del plugin.
#        - -Groups: cada grupo es otro proceso. Formato "carpeta" o "carpeta=busq1,busq2":
#          se revisan los exe, dll y pyd de esa carpeta [sin recursion] y cada import se
#          busca en la carpeta misma, en las de busqueda [relativas al paquete; "." es la
#          raiz] y en System32. Las carpetas de busqueda son las que el codigo le pone en el
#          PATH a ese proceso al lanzarlo.
#
# Los imports salen de `objdump -p`. Sale con 0 si esta todo, con 1 si falta algo.
param(
    [Parameter(Mandatory = $true)][string]$DeployDir,
    [Parameter(Mandatory = $true)][string]$Objdump,
    [string]$CompilarBat = '',
    [string[]]$PluginDirs = @('plugins'),
    [string[]]$Groups = @(),
    [string[]]$Require = @()
)

$ErrorActionPreference = 'Stop'
$problems = New-Object System.Collections.Generic.List[string]
$unreadable = New-Object System.Collections.Generic.List[string]

# `powershell -File` entrega "a,b,c" como UN solo string: se separa aca para que el llamador
# pueda pasar las listas con comas desde un .bat.
function Split-List([string[]]$values) {
    $out = @()
    foreach ($v in $values) { $out += ($v -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
    return , $out
}
function Split-Groups([string[]]$values) {
    # Los grupos se separan con ';' porque la lista de busqueda de cada uno ya usa ','.
    $out = @()
    foreach ($v in $values) { $out += ($v -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
    return , $out
}

if (-not (Test-Path -LiteralPath $DeployDir)) { Write-Host "ERROR: no existe $DeployDir"; exit 1 }
if (-not (Test-Path -LiteralPath $Objdump)) { Write-Host "ERROR: no se encontro objdump en $Objdump"; exit 1 }
$DeployDir = (Resolve-Path -LiteralPath $DeployDir).Path.TrimEnd('\')
$PluginDirs = Split-List $PluginDirs
$Require = Split-List $Require
$Groups = Split-Groups $Groups

# --- 1. Rutas obligatorias ---
foreach ($rel in $Require) {
    if (-not (Test-Path -LiteralPath (Join-Path $DeployDir $rel))) { $problems.Add("falta $rel") }
}

# --- 2. DEP_LIST de compilar.bat ---
if ($CompilarBat) {
    $depLine = Select-String -LiteralPath $CompilarBat -Pattern '^set "DEP_LIST=(.+)"\s*$' | Select-Object -First 1
    if (-not $depLine) {
        $problems.Add("no se pudo leer DEP_LIST de $CompilarBat")
    } else {
        $pluginRoot = if ($PluginDirs.Count -gt 0) { $PluginDirs[0] } else { '' }
        foreach ($dep in ($depLine.Matches[0].Groups[1].Value -split '\s+' | Where-Object { $_ })) {
            $rel = if ($dep.Contains('\') -and $pluginRoot) { Join-Path $pluginRoot $dep } else { $dep }
            if (-not (Test-Path -LiteralPath (Join-Path $DeployDir $rel))) {
                $problems.Add("falta $rel [esta en DEP_LIST de compilar.bat y no en el paquete]")
            }
        }
    }
}

# --- 3. Imports de cada binario ---
$system32 = Join-Path $env:SystemRoot 'System32'

# Devuelve $null si objdump no puede leer el archivo. Pasa con los exe a los que rcedit les
# cambio el icono: deja una cabecera PE que objdump no reconoce.
function Get-Imports([string]$path) {
    # Con 'Stop', Windows PowerShell 5.1 convierte cada linea de stderr de un exe nativo en
    # un error terminante, aunque se la redirija.
    $ErrorActionPreference = 'Continue'
    $output = & $Objdump -p $path 2>&1
    if ($LASTEXITCODE -ne 0) { return $null }
    # La coma evita que PowerShell desarme un array vacio en $null [un binario sin imports
    # no es un binario ilegible].
    return , @($output | ForEach-Object { "$_" } |
        Select-String -Pattern 'DLL Name: (.+)$' |
        ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() })
}

function Test-SystemDll([string]$name) {
    # Los api-ms-win-* / ext-ms-* son API sets: los resuelve el loader, no son archivos.
    return ($name -match '^(api|ext)-ms-') -or (Test-Path -LiteralPath (Join-Path $system32 $name))
}

$binaryExt = '.exe', '.dll', '.pyd'
$checkGroups = @()

$appFiles = @(Get-ChildItem -LiteralPath $DeployDir -File | Where-Object { $_.Extension -in '.exe', '.dll' })
foreach ($pd in $PluginDirs) {
    $dir = Join-Path $DeployDir $pd
    if (Test-Path -LiteralPath $dir) { $appFiles += @(Get-ChildItem -LiteralPath $dir -Recurse -File -Filter *.dll) }
}
$checkGroups += @{ Name = 'app'; Search = @($DeployDir); Files = $appFiles }

foreach ($spec in $Groups) {
    $parts = $spec -split '=', 2
    $rel = $parts[0].Trim()
    $dir = Join-Path $DeployDir $rel
    if (-not (Test-Path -LiteralPath $dir)) { $problems.Add("falta la carpeta $rel"); continue }
    $search = @($dir)
    if ($parts.Count -gt 1) {
        foreach ($s in ($parts[1] -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
            if ($s -eq '.') { $search += $DeployDir } else { $search += (Join-Path $DeployDir $s) }
        }
    }
    $files = @(Get-ChildItem -LiteralPath $dir -File | Where-Object { $_.Extension -in $binaryExt })
    $checkGroups += @{ Name = $rel; Search = $search; Files = $files }
}

$checked = 0
foreach ($group in $checkGroups) {
    foreach ($file in $group.Files) {
        $rel = $file.FullName.Substring($DeployDir.Length + 1)
        $imports = Get-Imports $file.FullName
        if ($null -eq $imports) { $unreadable.Add($rel); continue }
        $checked++
        foreach ($import in $imports) {
            $found = $false
            foreach ($s in $group.Search) {
                if (Test-Path -LiteralPath (Join-Path $s $import)) { $found = $true; break }
            }
            if ($found -or (Test-SystemDll $import)) { continue }
            $problems.Add("$rel importa $import, que no esta en el paquete ni en System32")
        }
    }
}

if ($problems.Count -gt 0) {
    Write-Host "Runtime del paquete INCOMPLETO:"
    $problems | Sort-Object -Unique | ForEach-Object { Write-Host "  - $_" }
    exit 1
}
Write-Host "Runtime del paquete completo: $checked binarios verificados."
if ($unreadable.Count -gt 0) {
    Write-Host "  [objdump no pudo leer, sin verificar: $($unreadable -join ', ')]"
}
exit 0
