#include "thetaexplorer/ColorUtils.h"
#include "thetaexplorer/Logger.h"
#include <QFile>
#include <QPair>
#include <QTextStream>
#include <QVector>
#include <algorithm>

namespace {

// Variables que puede usar el QSS. Mismo mecanismo que COLOR_VARS del Base.
const QVector<QPair<QString, QString>>& colorVars()
{
    static const QVector<QPair<QString, QString>> vars = {
        {"bg_principal",     ColorUtils::BG_PRINCIPAL},
        {"bg_items",         ColorUtils::BG_ITEMS},
        {"bg_tabs",          ColorUtils::BG_TABS},
        {"border_principal", ColorUtils::BORDER_PRINCIPAL},
        {"txt_principal",    ColorUtils::TXT_PRINCIPAL},
        {"txt_secundario",   ColorUtils::TXT_SECUNDARIO},
        {"txt_titulo",       ColorUtils::TXT_TITULO},
        {"violeta_oscuro",   ColorUtils::VIOLETA_OSCURO},
        {"violeta_claro",    ColorUtils::VIOLETA_CLARO},
        {"color_success",    ColorUtils::SUCCESS},
        {"color_warning",    ColorUtils::WARNING},
        {"color_error",      ColorUtils::ERROR_COLOR},
    };
    return vars;
}

} // namespace

QString ColorUtils::loadStyleSheet()
{
    QFile file(":/styles/dark_theme.qss");
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        LOGW("app") << "No se pudo abrir el QSS de recursos: " << file.fileName();
        return QString();
    }
    QString qss = QTextStream(&file).readAll();

    // Primero las claves largas, para que una clave que es prefijo de otra no se
    // reemplace antes (mismo criterio que el Base).
    QVector<QPair<QString, QString>> vars = colorVars();
    std::sort(vars.begin(), vars.end(), [](const auto& a, const auto& b) {
        return a.first.size() > b.first.size();
    });
    for (const auto& var : vars) {
        qss.replace(var.first, var.second);
    }
    return qss;
}
