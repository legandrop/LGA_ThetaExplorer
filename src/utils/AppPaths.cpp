#include "thetaexplorer/AppPaths.h"
#include "thetaexplorer/Logger.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>

namespace {
constexpr auto kAppFolder = "ThetaExplorer";
// Plist de las versiones que guardaban con QSettings nativo (Qt lo nombraba com.lga.<app>).
constexpr auto kLegacyPlist = "Library/Preferences/com.lga.ThetaMacExplorer.plist";
}

QString AppPaths::defaultLogFile()
{
#ifdef Q_OS_WIN
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("logs/debug.log"));
#else
    return QDir::homePath() + QStringLiteral("/Library/Logs/LGA/%1/debug.log").arg(kAppFolder);
#endif
}

QString AppPaths::settingsDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString AppPaths::settingsFile()
{
    return QDir(settingsDirectory()).filePath(QStringLiteral("settings.ini"));
}

QString AppPaths::updateCacheDirectory()
{
#ifdef Q_OS_WIN
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
#else
    return QDir::homePath() + QStringLiteral("/Library/Caches/LGA/%1").arg(kAppFolder);
#endif
}

void AppPaths::migrateLegacySettings()
{
    const QString iniPath = settingsFile();
    if (QFileInfo::exists(iniPath)) {
        return;
    }
    QDir().mkpath(settingsDirectory());

    // El nuevo nombre cambia AppDataLocation; preservar el INI anterior sin moverlo.
    QDir legacyDirectory(settingsDirectory());
    legacyDirectory.cdUp();
    const QString legacyIni = legacyDirectory.filePath(QStringLiteral("ThetaMacExplorer/settings.ini"));
    if (QFileInfo::exists(legacyIni)) {
        if (!QFile::copy(legacyIni, iniPath)) {
            LOGW("app") << "No se pudo copiar el INI anterior a " << iniPath;
        }
        return;
    }
#ifdef Q_OS_MAC
    const QString legacyPath = QDir::home().filePath(kLegacyPlist);
    if (!QFileInfo::exists(legacyPath)) {
        return;
    }

    QSettings legacy(legacyPath, QSettings::NativeFormat);
    QSettings ini(iniPath, QSettings::IniFormat);
    int copied = 0;
    for (const QString& key : legacy.allKeys()) {
        // El plist tambien trae claves globales del sistema (NSxxx, Apple*): solo se copian
        // las de la app.
        if (key.startsWith("window/") || key.startsWith("downloads/")) {
            ini.setValue(key, legacy.value(key));
            ++copied;
        }
    }
    ini.sync();
    LOGI("app") << "Settings migrados del plist anterior a " << iniPath
                << " (" << copied << " claves)";
#endif
}
