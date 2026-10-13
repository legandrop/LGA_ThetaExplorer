@echo off
setlocal
set "THETA_ROOT=%~dp0"
set "THETA_FLAGS="
:parse
if "%~1"=="" goto :run
if /I "%~1"=="--local" (set "THETA_FLAGS=%THETA_FLAGS% -LocalOnly" & shift & goto :parse)
if /I "%~1"=="--no-run" (set "THETA_FLAGS=%THETA_FLAGS% -NoRun" & shift & goto :parse)
if /I "%~1"=="--stage-only" (set "THETA_FLAGS=%THETA_FLAGS% -StageOnly" & shift & goto :parse)
if /I "%~1"=="--publish" (set "THETA_FLAGS=%THETA_FLAGS% -Publish" & shift & goto :parse)
if /I "%~1"=="--help" goto :help
echo ERROR: opcion desconocida: "%~1"
exit /b 1
:help
echo Uso: instalador.bat [--local] [--no-run] [--stage-only] [--publish]
echo Genera Release, setup y SHA256SUMS con la version actual, sin modificarla.
echo --publish requiere confirmacion interactiva antes de crear el release publico.
exit /b 0
:run
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%THETA_ROOT%tools\windows\build_installer.ps1" %THETA_FLAGS%
exit /b %ERRORLEVEL%
