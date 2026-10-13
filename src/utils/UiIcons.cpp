#include "thetaexplorer/UiIcons.h"
#include "thetaexplorer/Logger.h"
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

QPixmap UiIcons::pixmap(const QString& name, int logicalSize, const QColor& color, qreal dpr)
{
    const QString path = QStringLiteral(":/icons/ui/%1.svg").arg(name);
    QSvgRenderer renderer(path);
    if (!renderer.isValid()) {
        LOGW("ui") << "No se pudo cargar el icono SVG: " << path;
        return QPixmap();
    }

    // Supersampling x2 sobre la resolucion fisica y render con rect LOGICO explicito:
    // sin el rect, en Retina se ve solo el cuarto superior izquierdo del glyph
    // (ver hub Qt/C++, "SVG chico tenido sin recorte en QLabel").
    const qreal scale = qMax<qreal>(1.0, dpr) * 2.0;
    const int px = qRound(logicalSize * scale);
    QImage image(px, px, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        renderer.render(&p, QRectF(0, 0, px, px));
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(image.rect(), color);
    }

    const int target = qRound(logicalSize * qMax<qreal>(1.0, dpr));
    QPixmap out = QPixmap::fromImage(
        image.scaled(target, target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    out.setDevicePixelRatio(qMax<qreal>(1.0, dpr));
    return out;
}

QIcon UiIcons::buttonIcon(const QString& name, int logicalSize, const QColor& normal,
                          const QColor& disabled, qreal dpr)
{
    QIcon icon;
    icon.addPixmap(pixmap(name, logicalSize, normal, dpr), QIcon::Normal);
    icon.addPixmap(pixmap(name, logicalSize, disabled, dpr), QIcon::Disabled);
    return icon;
}
