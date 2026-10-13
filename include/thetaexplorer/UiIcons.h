#pragma once
#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

// Glyphs SVG monocromos de la interfaz (resources/icons/ui). Los SVG vienen en negro y se
// tinen al rasterizar, asi un mismo archivo sirve para cualquier color y estado.
namespace UiIcons {
    // Rasteriza `name` (ej. "folder") a `logicalSize` puntos, tenido de `color`.
    QPixmap pixmap(const QString& name, int logicalSize, const QColor& color, qreal dpr);

    // Icono para un QPushButton: normal y deshabilitado con su propio color.
    QIcon buttonIcon(const QString& name, int logicalSize, const QColor& normal,
                     const QColor& disabled, qreal dpr);
}
