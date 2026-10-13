#pragma once
#include <QString>
#include <QDir>
#include <QFileInfo>

// Rutas de datos de la app, con las ubicaciones de las apps LGA:
//   logs      ~/Library/Logs/LGA/ThetaExplorer/debug.log
//   settings  ~/Library/Application Support/LGA/ThetaExplorer/settings.ini
namespace AppPaths {
    // Requiere organizationName/applicationName ya seteados.
    QString defaultLogFile();

    // Requiere organizationName/applicationName ya seteados.
    QString settingsDirectory();
    QString settingsFile();
    QString updateCacheDirectory();
    QString mediaCacheDirectory();

    // Como en las otras apps: el updater nunca reemplaza un binario del arbol de desarrollo.
    inline bool runsFromDevelopmentTree(const QString& executablePath)
    {
        const QFileInfo info(executablePath);
        const QString canonical = info.canonicalFilePath();
        const QString path = canonical.isEmpty() ? QDir::cleanPath(executablePath) : canonical;
#ifdef Q_OS_MAC
        QDir bundle(QFileInfo(path).absolutePath());
        if (bundle.dirName() != "MacOS" || !bundle.cdUp() || !bundle.cdUp()
            || !bundle.dirName().endsWith(".app")) return true;
        const QString container = QFileInfo(bundle.absolutePath()).absoluteDir().dirName();
        if (container.contains("build", Qt::CaseInsensitive)
            || container.contains("deploy", Qt::CaseInsensitive)) return true;
#else
        // Un nombre de instalación puede contener build/deploy; sólo valen marcadores reales.
        QDir candidate(QFileInfo(path).absolutePath());
        for (int level = 0; level <= 2; ++level) {
            if (QFileInfo::exists(candidate.filePath(QStringLiteral("CMakeCache.txt")))) {
                QDir source(candidate);
                if (source.cdUp() && QFileInfo::exists(source.filePath(QStringLiteral("CMakeLists.txt")))
                    && QDir(source.filePath(QStringLiteral("src"))).exists()) return true;
            }
            if (!candidate.cdUp()) break;
        }
#endif
        const auto inside = [&path](const QString& root) {
            const QString clean = QDir::cleanPath(root);
            return path == clean || path.startsWith(clean + '/');
        };
#ifdef THETA_BUILD_TREE_DIR
        if (inside(QLatin1String(THETA_BUILD_TREE_DIR))) return true;
#endif
#ifdef THETA_SOURCE_TREE_DIR
        if (inside(QLatin1String(THETA_SOURCE_TREE_DIR))) return true;
#endif
        return false;
    }

    // Una sola vez: copia el INI anterior o, en macOS, el plist nativo. No borra el original.
    void migrateLegacySettings();
}
