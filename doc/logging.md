# Logging

## Resumen

La app ahora usa un logger centralizado en lugar de `qDebug()` suelto.

Archivos principales:

- `include/thetaexplorer/Logger.h`
- `src/utils/Logger.cpp`

## Objetivos

- unificar formato de logs
- mantener salida a archivo y a stderr
- filtrar por nivel
- filtrar por categorias
- evitar que cada archivo tenga su propio esquema informal

## Categorias actuales

- `app`
- `ui`
- `service`
- `bridge`
- `download`
- `export`

## Variables de entorno

- `THETA_LOG_LEVEL`
  Valores: `debug`, `info`, `warning`, `error`, `fatal`

- `THETA_LOG_CATEGORIES`
  Lista separada por comas, por ejemplo `ui,export,bridge`
  Usar `all` para habilitar todas

- `THETA_LOG_FILE`
  Ruta del archivo de log

- `THETA_LOG_MAX_BYTES`
  Tamano maximo antes de rotar el log

## Defaults

- archivo: `~/Library/Logs/LGA/ThetaExplorer/debug.log`, la ubicacion de las apps LGA (antes de v0.994 iba a `/tmp/ThetaExplorer.log`, que se borraba al reiniciar). La ruta la arma `AppPaths::defaultLogFile()` y el Help la muestra en «Paths».
- nivel: `debug`
- categorias: `all`
- rotacion: `5 MB`

## Uso en codigo

```cpp
LOGD("ui") << "Browse folder clicked";
LOGI("service") << "fileListUpdated:" << files.size();
LOGW("bridge") << "Device error:" << msg;
LOGE("export") << "Unexpected export failure";
```

## Criterio

- `LOGD` para trazas de flujo fino
- `LOGI` para eventos relevantes del sistema
- `LOGW` para fallas recuperables o comportamientos anormales
- `LOGE` para errores serios

## Ejemplo

```bash
THETA_LOG_LEVEL=debug THETA_LOG_CATEGORIES=ui,export ./compilar.sh
```
