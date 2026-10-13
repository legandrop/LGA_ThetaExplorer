#include "thetaexplorer/MainWindow.h"
#include "thetaexplorer/AppPaths.h"
#include "thetaexplorer/CameraFileInfo.h"
#include "thetaexplorer/ColorUtils.h"
#include "thetaexplorer/Logger.h"
#include "thetaexplorer/MediaAssetGroup.h"
#include <QApplication>
#include <QDateTime>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>

namespace {

// Inter embebida: el QSS declara una sola familia ("Inter"). Con una lista de familias Qt
// carga todas las fuentes del sistema al arrancar (ver hub Qt/C++, "font-family con lista").
void loadEmbeddedFonts()
{
    const QStringList fontFiles = {
        ":/fonts/Inter-Regular.ttf",
        ":/fonts/Inter-Medium.ttf",
        ":/fonts/Inter-Bold.ttf",
    };
    bool interLoaded = false;
    for (const QString& fontFile : fontFiles) {
        const int id = QFontDatabase::addApplicationFont(fontFile);
        if (id < 0) {
            LOGW("app") << "No se pudo cargar la fuente " << fontFile;
            continue;
        }
        interLoaded = interLoaded || QFontDatabase::applicationFontFamilies(id).contains("Inter");
    }
    if (!interLoaded) {
        LOGW("app") << "La familia Inter no quedo registrada: la UI cae a la fuente del sistema";
    }
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus, false);

    QApplication app(argc, argv);
    app.setApplicationName("ThetaExplorer");
    app.setApplicationDisplayName("Theta Explorer");
    // Version desde macro CMake (fuente unica de verdad en CMakeLists.txt via
    // target_compile_definitions). NO hardcodear el numero aca.
    app.setApplicationVersion(QLatin1String(THETAEXPLORER_VERSION));
    app.setOrganizationName("LGA");
    thetaexplorer::Logger::instance().initialize();
#ifndef Q_OS_MAC
    // En macOS NO se setea el icono en runtime: manda el del bundle.
    app.setWindowIcon(QIcon(":/icons/LGA_ThetaExplorer.ico"));
#endif

    LOGI("app") << "Theta Explorer iniciado en "
                << QDateTime::currentDateTime().toString()
                << " | " << thetaexplorer::Logger::instance().configurationSummary();

    AppPaths::migrateLegacySettings();

    // Register custom metatypes for cross-thread signal/slot use
    qRegisterMetaType<CameraFileInfo>("CameraFileInfo");
    qRegisterMetaType<QList<CameraFileInfo>>("QList<CameraFileInfo>");
    qRegisterMetaType<MediaAssetGroup>("MediaAssetGroup");
    qRegisterMetaType<QList<MediaAssetGroup>>("QList<MediaAssetGroup>");
    qRegisterMetaType<QPixmap>("QPixmap");

    loadEmbeddedFonts();
    QFont baseFont(QStringLiteral("Inter"));
    baseFont.setPixelSize(13);
    app.setFont(baseFont);
    app.setStyleSheet(ColorUtils::loadStyleSheet());

    MainWindow window;
    window.show();

    int ret = app.exec();
    LOGI("app") << "Theta Explorer cerrando";
    thetaexplorer::Logger::instance().shutdown();
    return ret;
}
