@echo off
setlocal
set "THETA_ROOT=%~dp0"
set "THETA_EXE=LGA_ThetaExplorer.exe"
set "THETA_BUILD=build"
set "THETA_TYPE=Debug"
set "THETA_DEPLOY_TYPE=debug"
set "THETA_QT_SUFFIX=d"
set "THETA_NO_RUN=false"
set "THETA_WAIT=false"
set "THETA_NO_DEPLOY=false"
set "THETA_CLEAN=false"
set "THETA_THREADS=%NUMBER_OF_PROCESSORS%"
if not defined THETA_THREADS set "THETA_THREADS=2"
:parse
if "%~1"=="" goto :configured
if /I "%~1"=="--release" (set "THETA_BUILD=build-release" & set "THETA_TYPE=Release" & set "THETA_DEPLOY_TYPE=release" & set "THETA_QT_SUFFIX=" & shift & goto :parse)
if /I "%~1"=="--no-run" (set "THETA_NO_RUN=true" & shift & goto :parse)
if /I "%~1"=="--wait" (set "THETA_WAIT=true" & shift & goto :parse)
if /I "%~1"=="--no-deploy" (set "THETA_NO_DEPLOY=true" & shift & goto :parse)
if /I "%~1"=="--force-clean" (set "THETA_CLEAN=true" & shift & goto :parse)
if /I "%~1"=="--parallel" (set "THETA_THREADS=%~2" & shift & shift & goto :parse)
if /I "%~1"=="--help" goto :help
if /I "%~1"=="-h" goto :help
echo ERROR: opcion desconocida: "%~1"
exit /b 1
:help
echo Uso: compilar.bat [--release] [--no-run] [--wait] [--no-deploy] [--parallel N] [--force-clean]
echo Debug incremental por defecto. Compila, copia runtime y abre la app sin retener la terminal.
exit /b 0
:configured
echo %THETA_THREADS%| findstr /R "^[1-9][0-9]*$" >nul
if errorlevel 1 (echo ERROR: --parallel necesita un numero positivo & exit /b 1)
if not defined THETA_QT_DIR set "THETA_QT_DIR=C:\Qt\6.5.3\mingw_64"
if not defined THETA_MINGW_BIN set "THETA_MINGW_BIN=C:\Qt\Tools\mingw1310_64\bin"
REM El SDK MinGW oficial puede traer solo Qt Release; CMake usa esa variante tambien para la app Debug.
if "%THETA_TYPE%"=="Debug" if not exist "%THETA_QT_DIR%\bin\Qt6Cored.dll" set "THETA_QT_SUFFIX="
if not exist "%THETA_QT_DIR%\bin\Qt6Core%THETA_QT_SUFFIX%.dll" (echo ERROR: Qt no existe en "%THETA_QT_DIR%" & exit /b 1)
if not exist "%THETA_MINGW_BIN%\g++.exe" (echo ERROR: MinGW no existe en "%THETA_MINGW_BIN%" & exit /b 1)
set "PATH=%THETA_MINGW_BIN%;%THETA_QT_DIR%\bin;C:\Qt\Tools\Ninja;%PATH%"
where cmake >nul 2>nul
if errorlevel 1 (echo ERROR: CMake no esta disponible & exit /b 1)
where ninja >nul 2>nul
if errorlevel 1 (echo ERROR: Ninja no esta disponible & exit /b 1)
REM Cerrar solo la copia que se va a reemplazar, incluyendo su helper de camara del mismo exe.
powershell -NoProfile -ExecutionPolicy Bypass -File "%THETA_ROOT%tools\windows\close_by_path.ps1" -ExeName "%THETA_EXE%" -ExactPath "%THETA_ROOT%%THETA_BUILD%\%THETA_EXE%"
if errorlevel 2 exit /b 1
cd /d "%THETA_ROOT%"
if "%THETA_CLEAN%"=="true" if exist "%THETA_BUILD%" rmdir /s /q "%THETA_BUILD%"
REM Reconfigurar preserva la cache y detecta tambien cambios de toolchain/prefix.
cmake -S . -B "%THETA_BUILD%" -G Ninja -DCMAKE_PREFIX_PATH="%THETA_QT_DIR%" -DCMAKE_CXX_COMPILER="%THETA_MINGW_BIN%\g++.exe" -DCMAKE_BUILD_TYPE=%THETA_TYPE%
if errorlevel 1 exit /b 1
cmake --build "%THETA_BUILD%" --parallel %THETA_THREADS%
if errorlevel 1 exit /b 1
if not exist "%THETA_BUILD%\%THETA_EXE%" (echo ERROR: no se genero el ejecutable & exit /b 1)
if "%THETA_NO_DEPLOY%"=="true" goto :validate_runtime
REM Copiar por rutas explicitas como en el Base: no depender de qtpaths del SDK.
for %%D in (libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do (
    call :copy_dep "%THETA_MINGW_BIN%\%%D" "%THETA_BUILD%\%%D"
    if errorlevel 1 exit /b 1
)
for %%D in (Core Gui Widgets Concurrent Network Svg) do (
    call :copy_dep "%THETA_QT_DIR%\bin\Qt6%%D%THETA_QT_SUFFIX%.dll" "%THETA_BUILD%\Qt6%%D%THETA_QT_SUFFIX%.dll"
    if errorlevel 1 exit /b 1
)
for %%D in (platforms\qwindows platforms\qoffscreen imageformats\qjpeg imageformats\qico imageformats\qsvg iconengines\qsvgicon tls\qschannelbackend) do (
    call :copy_dep "%THETA_QT_DIR%\plugins\%%D%THETA_QT_SUFFIX%.dll" "%THETA_BUILD%\%%D%THETA_QT_SUFFIX%.dll"
    if errorlevel 1 exit /b 1
)
REM Plugins opcionales presentes en el SDK; no se inventa una dependencia nueva.
for %%D in (tls\qcertonlybackend networkinformation\qnetworklistmanager) do (
    if exist "%THETA_QT_DIR%\plugins\%%D%THETA_QT_SUFFIX%.dll" call :copy_dep "%THETA_QT_DIR%\plugins\%%D%THETA_QT_SUFFIX%.dll" "%THETA_BUILD%\%%D%THETA_QT_SUFFIX%.dll"
    if errorlevel 1 exit /b 1
)

:validate_runtime
set "THETA_MISSING=false"
for %%D in (libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll Qt6Core%THETA_QT_SUFFIX%.dll Qt6Gui%THETA_QT_SUFFIX%.dll Qt6Widgets%THETA_QT_SUFFIX%.dll Qt6Concurrent%THETA_QT_SUFFIX%.dll Qt6Network%THETA_QT_SUFFIX%.dll Qt6Svg%THETA_QT_SUFFIX%.dll platforms\qwindows%THETA_QT_SUFFIX%.dll platforms\qoffscreen%THETA_QT_SUFFIX%.dll imageformats\qjpeg%THETA_QT_SUFFIX%.dll imageformats\qico%THETA_QT_SUFFIX%.dll tls\qschannelbackend%THETA_QT_SUFFIX%.dll) do if not exist "%THETA_BUILD%\%%D" (echo ERROR: falta %%D & set "THETA_MISSING=true")
if "%THETA_MISSING%"=="true" exit /b 1
if "%THETA_NO_RUN%"=="true" (echo Build listo, ejecucion omitida & exit /b 0)
set "THETA_APP=%THETA_ROOT%%THETA_BUILD%\%THETA_EXE%"
if "%THETA_WAIT%"=="true" goto :wait_app
REM Explorer desacopla la app de esta consola, como el resto de las apps LGA.
explorer.exe "%THETA_APP%"
exit /b 0
:wait_app
start "" /wait "%THETA_APP%"
exit /b %ERRORLEVEL%

:copy_dep
if not exist "%~1" (echo ERROR: no existe dependencia "%~1" & exit /b 1)
for %%F in ("%~2") do if not exist "%%~dpF" mkdir "%%~dpF"
copy /Y "%~1" "%~2" >nul
if errorlevel 1 exit /b 1
exit /b 0
