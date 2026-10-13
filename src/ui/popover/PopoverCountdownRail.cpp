#include "thetaexplorer/popover/PopoverCountdownRail.h"

#include <QPainter>
#include <QPainterPath>

PopoverCountdownRail::PopoverCountdownRail(QWidget *parent, int cornerRadius)
    : QWidget(parent)
    , m_fraction(1.0)
    , m_cornerRadius(cornerRadius)
    , m_trackColor(0x2B, 0x2B, 0x2B)
    , m_fillColor(0x77, 0x4D, 0xCB)   // el violeta de acento de la app
{
    setFixedHeight(kHeight);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void PopoverCountdownRail::setFraction(qreal fraction)
{
    const qreal clamped = qBound(0.0, fraction, 1.0);
    // Corta si el riel se ve IGUAL que antes: la animacion emite ~60 veces por segundo y no
    // tiene sentido repintar si no cambia ni un pixel. Cuesta nada.
    const int before = qRound(width() * m_fraction);
    const int after  = qRound(width() * clamped);
    m_fraction = clamped;
    if (before != after) {
        update();
    }
}

void PopoverCountdownRail::attachToBottom()
{
    QWidget *host = parentWidget();
    if (!host) return;
    setGeometry(0, host->height() - kHeight, host->width(), kHeight);
    raise();
}

void PopoverCountdownRail::setTrackColor(const QColor &color)
{
    m_trackColor = color;
    update();
}

void PopoverCountdownRail::setFillColor(const QColor &color)
{
    m_fillColor = color;
    update();
}

void PopoverCountdownRail::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // El riel esta pegado al borde de abajo de un contenedor con esquinas redondeadas, asi
    // que se recorta contra ESE rectangulo -expresado en coordenadas propias- y no contra el
    // suyo: si no, las dos puntas asoman por fuera de la curva.
    if (QWidget *host = parentWidget()) {
        QRectF hostRect(0, 0, host->width(), host->height());
        hostRect.translate(-x(), -y());
        QPainterPath clip;
        clip.addRoundedRect(hostRect.adjusted(0.5, 0.5, -0.5, -0.5),
                            m_cornerRadius, m_cornerRadius);
        painter.setClipPath(clip);
    }

    painter.fillRect(rect(), m_trackColor);
    const int filled = qRound(width() * m_fraction);
    if (filled > 0) {
        painter.fillRect(QRect(0, 0, filled, height()), m_fillColor);
    }
}
