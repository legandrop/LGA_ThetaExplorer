#pragma once

#include <QWidget>
#include <QPoint>

class QVBoxLayout;
class QLabel;
class QPushButton;
class QVariantAnimation;
class PopoverCountdownRail;

/**
 * @brief Aviso flotante no modal: aparece, no roba el foco y (si se pide) se va solo.
 *
 * Es la contraparte de un dialogo modal. Criterio: si el mensaje NO cambia lo que el usuario
 * va a hacer a continuacion, va aca y no en un dialogo.
 *
 * - "Listo / guardado / descargado": `showPopover(4000)`, se cierra solo.
 * - Error o advertencia con detalle: `showPopover()` (0), se queda hasta que el usuario lo
 *   cierre. **Un error nunca lleva auto-cierre**: el detalle tiene que poder leerse y copiarse.
 *
 * Es una ventana `Qt::Tool` propia (frameless, translucida, siempre arriba) hija de `parent`:
 * se centra sobre la ventana de `parent` y se destruye con ella. Se autodestruye al cerrarse
 * (`WA_DeleteOnClose`), asi que quien guarde el puntero tiene que usar `QPointer`.
 *
 * El texto es HTML. Todo el popover es arrastrable menos la cruz. Se cierra con la cruz, con
 * Escape (solo si tiene el foco: no lo toma al aparecer) o con `closePopover()`.
 *
 * Referencia de diseño y trampas: `Doc_MessagePopover.md` del Base LGA.
 */
class MessagePopover : public QWidget
{
    Q_OBJECT

public:
    explicit MessagePopover(QWidget *parent = nullptr);
    ~MessagePopover() override;

    /**
     * @brief Establece el mensaje a mostrar (HTML). Re-mide el popover.
     */
    void setMessage(const QString& message);

    /**
     * @brief Muestra el popover centrado sobre la ventana del padre, o en la pantalla primaria
     *        si no tiene padre.
     *
     * @param autoCloseMs Milisegundos hasta que se cierre solo. 0 = se queda hasta que el
     *        usuario lo cierre. Con un valor > 0 aparece un riel de 3 px en el borde inferior
     *        que se consume, y la cuenta se PAUSA mientras el mouse esta encima.
     *
     * El auto-cierre vive aca y no en cada llamador a proposito: cuando dependia de que cada
     * uno pusiera su propio `QTimer`, la mayoria se lo olvidaba.
     */
    void showPopover(int autoCloseMs = 0);

    /**
     * @brief Cierra el popover (y con eso lo destruye)
     */
    void closePopover();

    // NO hay setters de sombra, y es a proposito: la sombra es un pixmap cacheado que sale de
    // las constantes de `PopoverShadow.h`, el punto unico de la app.

    /**
     * @brief Etiqueta que muestra el mensaje
     *
     * Expuesta para que quien arma el popover pueda hacerla seleccionable
     * (`setTextInteractionFlags()`) o conectar `linkActivated` (por ejemplo, un link "Details"
     * inyectado en el HTML).
     */
    QLabel* messageLabel() const;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

    /**
     * @brief Dibuja la sombra de la tarjeta, con el pixmap cacheado de `PopoverShadow`.
     *
     * No es un `QGraphicsDropShadowEffect` a proposito: un efecto sobre un `QWidget` no cachea
     * nada, asi que cada frame del riel de auto-cierre re-blureaba el popover entero.
     */
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

    /// Pausa la cuenta regresiva del auto-cierre mientras el mouse esta encima.
    void enterEvent(QEnterEvent *event) override;

    /// Reanuda la cuenta regresiva al salir el mouse.
    void leaveEvent(QEvent *event) override;

    /// Red de seguridad de la pausa: reanuda tambien al desactivarse la app o al esconderse el
    /// popover, porque `leaveEvent` solo no siempre llega.
    bool event(QEvent *event) override;

private:
    void setupUI();

    /// Mide el texto y fija el tamaño de la tarjeta. No se llama `adjustSize()` para no tapar
    /// el `QWidget::adjustSize()` de la base, que se sigue usando adentro.
    void updateContentSize();

    /// Reubica la cruz en la esquina superior derecha de la tarjeta, y el riel abajo.
    void layoutOverlays();

    /// true si un punto (coordenadas del popover) inicia un arrastre: todo menos la cruz.
    bool isInDragArea(const QPoint& point) const;

    /// Arranca la cuenta regresiva del auto-cierre si se pidio una.
    void startAutoClose();

    /// Reanuda la cuenta si estaba pausada. Idempotente.
    void resumeAutoClose();

    QWidget *m_contentWidget;           ///< Tarjeta con fondo y bordes redondeados
    QVBoxLayout *m_mainLayout;          ///< Layout de la ventana (reserva el margen de la sombra)
    QVBoxLayout *m_contentLayout;       ///< Layout de la tarjeta
    QPushButton *m_closeButton;         ///< Cruz de cierre, flotante sobre la tarjeta
    QLabel *m_messageLabel;             ///< Texto del mensaje

    bool m_isDragging;                  ///< Hay un arrastre en curso
    QPoint m_dragStartPosition;         ///< Offset del mouse respecto de la esquina al arrastrar

    PopoverCountdownRail *m_rail;       ///< Riel de la cuenta regresiva (oculto si no hay auto-cierre)
    QVariantAnimation *m_autoCloseAnim; ///< Anima el riel Y marca el cierre: una sola fuente de tiempo
    int m_autoCloseMs;                  ///< Duracion pedida del auto-cierre, 0 = sin auto-cierre
};
