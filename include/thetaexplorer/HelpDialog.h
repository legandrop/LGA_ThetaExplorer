#pragma once
#include <QDialog>
class LgaUpdateService;

// Help de la app: nombre y version, autor con link a la web, atajos y rutas clicables.
// Se cierra con Esc o con el boton de la ventana, como el Help del Player de MediaTools.
class HelpDialog : public QDialog
{
    Q_OBJECT
public:
    explicit HelpDialog(const QString& downloadFolder, QWidget* parent = nullptr,
                        LgaUpdateService* updates = nullptr);
};
