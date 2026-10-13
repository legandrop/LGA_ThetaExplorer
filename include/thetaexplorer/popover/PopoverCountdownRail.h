#pragma once

#include <QColor>
#include <QWidget>

/**
 * @brief Riel de 3 px pegado al borde inferior de un popover, que se consume con la cuenta
 *        regresiva de su auto-cierre.
 *
 * NO acepta eventos de mouse (`WA_TransparentForMouseEvents`): si los tomara, arrastrar el
 * popover desde ahi dejaria de funcionar y el hover que pausa la cuenta se cortaria al pasar
 * por encima suyo.
 *
 * Se posiciona a mano (no va en un layout) con `attachToBottom()`, y recorta su pintado contra
 * el rectangulo REDONDEADO del contenedor para no asomar por fuera de la curva en las dos
 * puntas de abajo.
 *
 * Quien lo usa es responsable de moverle la fraccion; `MessagePopover` lo hace desde el
 * `QVariantAnimation` que ademas dispara el cierre, para que la cuenta y el riel no puedan
 * desfasarse.
 */
class PopoverCountdownRail : public QWidget
{
    Q_OBJECT

public:
    /// Alto del riel en px.
    static constexpr int kHeight = 3;

    /**
     * @param parent Contenedor del popover (el widget con el fondo y las esquinas redondeadas)
     * @param cornerRadius Radio de esas esquinas, para recortar el pintado
     */
    explicit PopoverCountdownRail(QWidget *parent, int cornerRadius = 8);

    /// Fraccion pendiente, de 1.0 (recien empieza) a 0.0 (se acabo).
    void setFraction(qreal fraction);

    /// Lo estira al ancho del contenedor y lo pega a su borde inferior.
    void attachToBottom();

    void setTrackColor(const QColor &color);
    void setFillColor(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    qreal m_fraction;
    int m_cornerRadius;
    QColor m_trackColor;
    QColor m_fillColor;
};
