#Requires -Version 7.0
param([Parameter(Mandatory = $true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$start = [Diagnostics.ProcessStartInfo]::new([IO.Path]::GetFullPath($Executable), '--camera-worker')
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardInput = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.StandardInputEncoding = [Text.UTF8Encoding]::new($false)
$start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
# El runtime debe salir del deploy, sin DLL ni plugins prestados por el SDK.
$runtimePath = [IO.Path]::GetDirectoryName($start.FileName)
$start.Environment['PATH'] = "$runtimePath;$env:SystemRoot\System32;$env:SystemRoot"
$start.Environment.Remove('QT_PLUGIN_PATH') | Out-Null
$start.Environment.Remove('QT_QPA_PLATFORM_PLUGIN_PATH') | Out-Null
$process = [Diagnostics.Process]::new()
$process.StartInfo = $start
$epoch = [Guid]::NewGuid().ToString('D')
$started = $false
try {
    if (-not $process.Start()) { throw 'Camera worker did not start' }
    $started = $true
    $errors = $process.StandardError.ReadToEndAsync()
    $process.StandardInput.AutoFlush = $true
    $process.StandardInput.WriteLine((@{ v = 1; epoch = $epoch; id = '1'; op = 'browse' } | ConvertTo-Json -Compress))
    $done = $false
    for ($i = 0; $i -lt 20; $i++) {
        $line = $process.StandardOutput.ReadLineAsync()
        if (-not $line.Wait(15000)) { throw 'Camera browse timed out' }
        if ($null -eq $line.Result -or $line.Result.Length -gt 8388608) { throw 'Invalid camera worker response' }
        $packet = $line.Result | ConvertFrom-Json
        if ($packet.v -ne 1 -or $packet.epoch -ne $epoch -or $packet.id -notin '0', '1') { throw 'Camera worker protocol mismatch' }
        if ($packet.event -eq 'error') { throw "Camera worker error: $($packet.message)" }
        if ($packet.event -eq 'done' -and $packet.id -eq '1') { $done = $true; break }
    }
    if (-not $done) { throw 'Camera browse did not complete' }
    $process.StandardInput.WriteLine((@{ v = 1; epoch = $epoch; id = '0'; op = 'stop' } | ConvertTo-Json -Compress))
    $process.StandardInput.Close()
    if (-not $process.WaitForExit(7000)) { throw 'Camera worker did not stop' }
    if ($process.ExitCode -ne 0) { throw "Camera worker exited with $($process.ExitCode): $($errors.Result)" }
    Write-Host 'Camera worker browse/stop PASS (no physical camera)'
} finally {
    if ($started -and -not $process.HasExited) {
        $process.Kill($true)
        $process.WaitForExit(5000) | Out-Null
    }
    $process.Dispose()
}

# Arranque real de QApplication con el plugin copiado; no requiere escritorio ni camara.
$guiStart = [Diagnostics.ProcessStartInfo]::new($start.FileName, '-platform offscreen')
$guiStart.UseShellExecute = $false
$guiStart.CreateNoWindow = $true
$guiStart.RedirectStandardOutput = $true
$guiStart.RedirectStandardError = $true
$guiStart.Environment['PATH'] = $start.Environment['PATH']
$guiStart.Environment.Remove('QT_PLUGIN_PATH') | Out-Null
$guiStart.Environment['QT_QPA_PLATFORM_PLUGIN_PATH'] = Join-Path $runtimePath 'platforms'
$smokeRoot = Join-Path ([IO.Path]::GetTempPath()) ('ThetaExplorer-smoke-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $smokeRoot | Out-Null
$guiStart.Environment['THETA_LOG_FILE'] = Join-Path $smokeRoot 'startup.log'
$gui = [Diagnostics.Process]::new()
$gui.StartInfo = $guiStart
$guiStarted = $false
try {
    if (-not $gui.Start()) { throw 'Application did not start' }
    $guiStarted = $true
    $guiErrors = $gui.StandardError.ReadToEndAsync()
    $guiOutput = $gui.StandardOutput.ReadToEndAsync()
    if ($gui.WaitForExit(5000)) { throw "Application exited during startup ($($gui.ExitCode)): $($guiErrors.Result)" }
    $startupLog = Get-Content -Raw -LiteralPath $guiStart.Environment['THETA_LOG_FILE']
    if ($startupLog -notmatch 'Theta Explorer iniciado') { throw 'Application did not initialize its logger' }
    Write-Host 'Application offscreen startup PASS with deployed runtime'
} finally {
    if ($guiStarted -and -not $gui.HasExited) {
        $gui.Kill($true)
        if (-not $gui.WaitForExit(5000)) { throw 'Smoke application did not stop' }
    }
    $gui.Dispose()
    Remove-Item -LiteralPath $smokeRoot -Recurse -Force
}
