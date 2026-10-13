#pragma once
#include <QString>

// Paleta de la app. Los nombres y valores base son los de COLOR_VARS de LGA_Base_QT_C_Py;
// los que no existen en el Base (estados) se agregan abajo. El QSS (resources/styles/
// dark_theme.qss) usa estos mismos nombres como variables y loadStyleSheet() los reemplaza.
namespace ColorUtils {
    // Base LGA
    inline constexpr auto BG_PRINCIPAL     = "#161616";
    inline constexpr auto BG_ITEMS         = "#1d1d1d";
    inline constexpr auto BG_TABS          = "#101010";
    inline constexpr auto BORDER_PRINCIPAL = "#303030";
    inline constexpr auto TXT_PRINCIPAL    = "#b2b2b2";
    inline constexpr auto TXT_SECUNDARIO   = "#8f8f8f";
    inline constexpr auto TXT_TITULO       = "#cccccc";
    inline constexpr auto VIOLETA_OSCURO   = "#443a91";
    inline constexpr auto VIOLETA_CLARO    = "#774dcb";

    // Estados (barra de estado, bateria, avisos)
    inline constexpr auto SUCCESS     = "#4caf7d";
    inline constexpr auto WARNING     = "#e0a458";
    inline constexpr auto ERROR_COLOR = "#cf6679";

    // Lee el QSS de recursos, reemplaza las variables de color y lo devuelve.
    // Si el recurso no se puede abrir devuelve un string vacio y deja un warning en el log.
    QString loadStyleSheet();
}
