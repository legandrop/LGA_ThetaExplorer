#pragma once
#include <QDialog>
#include <QList>
#include <QString>

class QPushButton;

// Cartel de confirmacion con el mismo chrome que los dialogos del Player de MediaTools
// (Replace track, Import Shot Review): fondo bg_principal, icono de 44 px, titulo y
// subtitulo, una fila que dice que hace cada tecla y botones del mismo ancho sin borde.
// Que tecla hace que lo dicen el texto del boton y la fila de atajos, nunca un contorno.
class ConfirmDialog : public QDialog
{
    Q_OBJECT
public:
    enum class ButtonStyle { Neutral, Action, Danger };

    struct Button {
        QString     text;
        ButtonStyle style = ButtonStyle::Neutral;
        Qt::Key     shortcut = Qt::Key_unknown;
    };

    struct Spec {
        QString       iconName;      // glyph de resources/icons/ui, sin extension
        QString       title;
        QString       subtitle;
        QString       hintHtml;      // fila de atajos; las teclas van en <b>
        QString       contentHtml;   // notas opcionales, con scroll
        QString       details;       // detalle tecnico plano, inicialmente oculto
        QList<Button> buttons;       // de izquierda a derecha
        int           defaultIndex = -1;   // el que responde a Enter
        int           escapeIndex  = -1;   // lo que devuelve Esc o cerrar la ventana
        QList<int>    noFocusIndexes;      // botones que solo responden al click (destructivos)
    };

    // Devuelve el indice del boton apretado, o spec.escapeIndex.
    static int ask(QWidget* parent, const Spec& spec);

private:
    ConfirmDialog(const Spec& spec, QWidget* parent);
    int m_result = -1;
};
