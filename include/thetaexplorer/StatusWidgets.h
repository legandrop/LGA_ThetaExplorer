#pragma once
#include <QColor>
#include <QWidget>

class QLabel;
class QTimer;

// Punto de estado de la camara. Se pinta con QPainter y antialias: un circulo con
// border-radius de QSS no antialiasea (ver hub Qt/C++).
class StatusDot : public QWidget
{
    Q_OBJECT
public:
    enum class Mode { Off, Connected, Searching };

    explicit StatusDot(QWidget* parent = nullptr);
    void setMode(Mode mode);
    QSize sizeHint() const override { return QSize(14, 14); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Mode    m_mode = Mode::Off;
    int     m_angle = 0;
    QTimer* m_timer = nullptr;
};

// Lo que muestra la grilla cuando no hay nada que mostrar: icono, titulo, texto y,
// opcionalmente, la fila "buscando".
class EmptyStateWidget : public QWidget
{
    Q_OBJECT
public:
    explicit EmptyStateWidget(QWidget* parent = nullptr);
    void setContent(const QString& title, const QString& text, bool searching);

private:
    QLabel*    m_title     = nullptr;
    QLabel*    m_text      = nullptr;
    QWidget*   m_searchRow = nullptr;
    StatusDot* m_searchDot = nullptr;
};
