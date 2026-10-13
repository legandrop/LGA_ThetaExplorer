#include "thetaexplorer/popover/PopoverCloseButton.h"

#include <QFocusEvent>
#include <QPainter>

namespace {

// El glifo es una fraccion de la caja, no un valor fijo: asi un boton de 24 y uno de 20 se
// ven como el MISMO boton a dos tamaños. 0.425 sale del prototipo aprobado (caja 20, cruz
// 8.5). El trazo si es fijo: a 1.5 px se lee parejo en los dos tamaños, y engordarlo con la
// caja lo devolvia al aspecto pesado del icono.
constexpr qreal kGlyphRatio  = 0.425;
constexpr qreal kStrokeWidth = 1.5;

}  // namespace

PopoverCloseButton::PopoverCloseButton(int size, QWidget *parent)
    : QPushButton(parent)
    , m_crossColor(0x9A, 0x9A, 0x9A)
    , m_hoverColor(0x3A, 0x3A, 0x3A)
    , m_pressedColor(0x4A, 0x4A, 0x4A)
{
    setFixedSize(size, size);
    setCursor(Qt::PointingHandCursor);
    // El focusPolicy queda en el default de QPushButton A PROPOSITO: con NoFocus la cruz sale
    // del tab-order, y en un popover que es un formulario es la unica forma de cerrarlo sin
    // mouse.
    setAttribute(Qt::WA_Hover);   // sin esto el hover no dispara repintado
}

void PopoverCloseButton::setCrossColor(const QColor &color)
{
    m_crossColor = color;
    update();
}

void PopoverCloseButton::setHoverColor(const QColor &color)
{
    m_hoverColor = color;
    update();
}

void PopoverCloseButton::setPressedColor(const QColor &color)
{
    m_pressedColor = color;
    update();
}

void PopoverCloseButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // El estado apretado tambien se pinta aca: como este `paintEvent` no llama al de la clase
    // base, un `:pressed` del QSS no aplicaria.
    if (isDown()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_pressedColor);
        painter.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);
    } else if (underMouse()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_hoverColor);
        painter.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);
    }

    // Anillo de foco. Como este `paintEvent` no llama al de la clase base, el foco tampoco se
    // dibuja solo: sin esto, quien llega con Tab no ve donde esta parado.
    //
    // Va por `m_showFocusRing` y NO por `hasFocus()`: al abrirse un popover, Qt le puede dar el
    // foco al primer widget de la cadena, que suele ser este boton. Con `hasFocus()` la cruz se
    // veria siempre adentro de un circulo. El anillo es para quien navega con teclado.
    if (m_showFocusRing) {
        QPen focusPen(m_crossColor);
        focusPen.setWidthF(1.0);
        painter.setPen(focusPen);
        painter.setBrush(Qt::NoBrush);
        const QRectF ring = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.drawRoundedRect(ring, ring.height() / 2.0, ring.height() / 2.0);
    }

    QPen pen(m_crossColor);
    pen.setWidthF(kStrokeWidth);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);

    const QPointF center = QRectF(rect()).center();
    const qreal arm = (width() * kGlyphRatio) / 2.0;
    painter.drawLine(QPointF(center.x() - arm, center.y() - arm),
                     QPointF(center.x() + arm, center.y() + arm));
    painter.drawLine(QPointF(center.x() + arm, center.y() - arm),
                     QPointF(center.x() - arm, center.y() + arm));
}

void PopoverCloseButton::focusInEvent(QFocusEvent *event)
{
    // Solo los motivos de TECLADO encienden el anillo. `ActiveWindowFocusReason` (el que llega
    // al abrirse la ventana) y `OtherFocusReason` no cuentan: son foco que el usuario no pidio.
    const Qt::FocusReason reason = event->reason();
    m_showFocusRing = (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason
                       || reason == Qt::ShortcutFocusReason);
    QPushButton::focusInEvent(event);
    update();
}

void PopoverCloseButton::focusOutEvent(QFocusEvent *event)
{
    m_showFocusRing = false;
    QPushButton::focusOutEvent(event);
    update();
}
