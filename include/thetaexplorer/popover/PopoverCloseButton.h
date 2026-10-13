#pragma once

#include <QColor>
#include <QPushButton>

/**
 * @brief Boton de cierre de los popovers: la cruz se DIBUJA, no es un icono.
 *
 * Un SVG de cierre pensado para botones grandes, metido a 16-24 px, queda pesado y se come la
 * esquina del popover. Aca el glifo es proporcional a la caja y el trazo es fijo.
 *
 * **Un `paintEvent` propio que no llama al de la clase base ignora los backgrounds del QSS.**
 * O sea que reglas `:hover` / `:pressed` en un QSS para este boton no hacen nada: el fondo del
 * hover y del pressed lo pinta esta clase. Por eso los colores son parametros.
 */
class PopoverCloseButton : public QPushButton
{
    Q_OBJECT

public:
    /**
     * @param size Lado de la caja en px (`MessagePopover` usa 20)
     * @param parent Widget padre
     */
    explicit PopoverCloseButton(int size = 20, QWidget *parent = nullptr);

    /// Color de la cruz. Default #9A9A9A.
    void setCrossColor(const QColor &color);

    /// Color del circulo que aparece detras al pasar el mouse. Default #3A3A3A.
    void setHoverColor(const QColor &color);

    /// Color del circulo mientras el boton esta apretado. Default #4A4A4A.
    void setPressedColor(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;
    // El anillo de foco se dibuja SOLO cuando el foco llego por teclado. Ver el comentario
    // de `focusInEvent` en el .cpp.
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    QColor m_crossColor;
    bool m_showFocusRing = false;
    QColor m_hoverColor;
    QColor m_pressedColor;
};
