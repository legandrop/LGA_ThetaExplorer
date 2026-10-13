#include "thetaexplorer/popover/MessagePopover.h"
#include "thetaexplorer/ColorUtils.h"

#include "thetaexplorer/Logger.h"
#include "thetaexplorer/popover/PopoverCloseButton.h"
#include "thetaexplorer/popover/PopoverCountdownRail.h"
#include "thetaexplorer/popover/PopoverShadow.h"

#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QRect>
#include <QResizeEvent>
#include <QScreen>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QtMath>

namespace {

// ---------------------------------------------------------------------------------------
// Medidas del popover. Salen del prototipo aprobado; `message_popover.qss` usa los mismos
// valores, asi que si se cambia uno hay que cambiar los dos.
// ---------------------------------------------------------------------------------------
constexpr int kContentRadius = 8;     // igual que el border-radius del QSS
constexpr int kLabelPadding  = 8;     // igual que el padding del QLabel en el QSS
constexpr int kMinTextHeight = 60;    // igual que el min-height del QLabel en el QSS
constexpr int kContentMargin = 22;    // aire de la tarjeta, igual en los 4 lados
constexpr int kMinWidth      = 250;
constexpr int kMaxWidth      = 460;
// `QTextDocument::idealWidth()` da el ancho JUSTO, y el QLabel maqueta con su propio
// documento: un sub-pixel de diferencia alcanza para que envuelva igual. Medido: el ancho
// calculado le dejaba al texto exactamente los pixeles que pedia, y partia la linea lo mismo.
// Estos 4 px son esa holgura.
constexpr int kWidthSlack    = 4;

// Boton de cierre: caja de 20. La cruz la dibuja PopoverCloseButton.
constexpr int kCloseSize   = 20;
constexpr int kCloseMargin = 6;

const char *const kStyleResource = ":/styles/message_popover.qss";
const char *const kLogCategory   = "popover";

}  // namespace

MessagePopover::MessagePopover(QWidget *parent)
    : QWidget(parent)
    , m_contentWidget(nullptr)
    , m_mainLayout(nullptr)
    , m_contentLayout(nullptr)
    , m_closeButton(nullptr)
    , m_messageLabel(nullptr)
    , m_isDragging(false)
    , m_dragStartPosition()
    , m_rail(nullptr)
    , m_autoCloseAnim(nullptr)
    , m_autoCloseMs(0)
{
    // Ventana sin marco y translucida.
    //
    // `NoDropShadowWindowHint` es obligatorio en macOS: la sombra la dibuja Qt, y sin el flag
    // el sistema suma la suya (en Tahoe, ademas, un borde claro de Liquid Glass por fuera).
    //
    // `WindowDoesNotAcceptFocus` + `WA_ShowWithoutActivating` NO son un detalle: un `Qt::Tool`
    // es una ventana aparte y sin esto se volvia la ventana ACTIVA de la app. El usuario sigue
    // trabajando en la ventana principal y su primer click se gastaba en re-activarla.
    //
    // `Qt::Tool` en macOS es un NSPanel que AppKit esconde cuando la app pierde el frente, y lo
    // vuelve a mostrar al recuperarlo. Para un aviso eso es lo correcto (si no, un
    // `WindowStaysOnTopHint` flotaria por encima de las otras apps), asi que NO se le pone
    // `WA_MacAlwaysShowToolWindow`.
    setWindowFlags(Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::Tool
                   | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating);

    // El QSS global de la app ya aplica a este widget; el del popover lo completa.
    QFile styleFile(QString::fromLatin1(kStyleResource));
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    } else {
        LOGW(kLogCategory) << "No se pudo cargar " << kStyleResource
                           << " (falta en el .qrc?): el popover sale sin su estilo";
    }

    setupUI();
}

MessagePopover::~MessagePopover() = default;

void MessagePopover::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    // El aire transparente donde se dibuja la sombra. Tiene que ser EXACTAMENTE
    // `kBlurRadius`: `paintRoundedShadow()` usa esa misma constante como margen propio y
    // arranca el pixmap en `rect.topLeft() - kBlurRadius`. Si difieren, el halo se recorta
    // contra el borde de la ventana.
    m_mainLayout->setContentsMargins(PopoverShadow::kBlurRadius, PopoverShadow::kBlurRadius,
                                     PopoverShadow::kBlurRadius, PopoverShadow::kBlurRadius);
    m_mainLayout->setSpacing(0);
    m_mainLayout->setAlignment(Qt::AlignCenter);

    // Tarjeta con fondo y bordes (los pone el QSS por objectName). La sombra NO se aplica
    // como efecto: la dibuja `paintEvent()`.
    m_contentWidget = new QWidget(this);
    m_contentWidget->setObjectName("messagePopoverContentWidget");

    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(kContentMargin, kContentMargin,
                                        kContentMargin, kContentMargin);
    m_contentLayout->setSpacing(0);

    m_messageLabel = new QLabel(m_contentWidget);
    m_messageLabel->setObjectName("messagePopoverTextLabel");
    m_messageLabel->setWordWrap(true);
    // Centrado vertical: con una sola linea de texto y el `min-height: 60px` del QSS, el
    // AlignTop dejaba el mensaje pegado arriba y todo el aire abajo.
    m_messageLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_messageLabel->setTextFormat(Qt::RichText);
    m_contentLayout->addWidget(m_messageLabel);

    // Riel del auto-cierre: existe siempre pero arranca oculto; solo aparece si
    // `showPopover()` recibe una duracion.
    m_rail = new PopoverCountdownRail(m_contentWidget, kContentRadius);
    m_rail->hide();

    // Cruz de cierre, flotante (fuera del layout) en la esquina de la tarjeta.
    m_closeButton = new PopoverCloseButton(kCloseSize, m_contentWidget);
    m_closeButton->setObjectName("messagePopoverCloseButton");
    connect(m_closeButton, &QPushButton::clicked, this, &MessagePopover::closePopover);

    m_mainLayout->addWidget(m_contentWidget);

    // Tamaño inicial; `updateContentSize()` lo corrige en cuanto hay texto.
    m_contentWidget->setFixedSize(kMinWidth, 120);
    layoutOverlays();
}

void MessagePopover::layoutOverlays()
{
    if (!m_contentWidget) return;

    if (m_closeButton) {
        m_closeButton->move(m_contentWidget->width() - m_closeButton->width() - kCloseMargin,
                            kCloseMargin);
        m_closeButton->raise();
    }
    if (m_rail) {
        m_rail->attachToBottom();
    }
}

void MessagePopover::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutOverlays();
}

void MessagePopover::setMessage(const QString& message)
{
    if (!m_messageLabel) return;

    m_messageLabel->setTextFormat(Qt::RichText);

    QString htmlMessage = message;
    htmlMessage.replace("<br>", "<br/>");

    // Si no viene como documento HTML completo, se envuelve con el color de texto por defecto.
    if (!htmlMessage.contains("<html>")) {
        htmlMessage = QString("<html><body style='color:%1;'>%2</body></html>")
                          .arg(ColorUtils::TXT_PRINCIPAL, htmlMessage);
    }

    // Un text-align:right heredado de otro lado rompe el layout del aviso.
    htmlMessage.replace("style='text-align:right'", "");
    htmlMessage.replace("style=\"text-align:right\"", "");

    m_messageLabel->setText(htmlMessage);

    // Re-medir despues de que el label procese el HTML.
    QTimer::singleShot(10, this, &MessagePopover::updateContentSize);
}

void MessagePopover::updateContentSize()
{
    if (!m_messageLabel || !m_contentWidget) return;

    // El ancho se MIDE con un QTextDocument, no con `QLabel::sizeHint()`.
    //
    // Con `wordWrap` activo, el sizeHint de un label rich text ya viene envuelto por una
    // heuristica de Qt, asi que devolvia un ancho chico y quedaba clavado contra el minimo:
    // un mensaje corto salia partido en dos lineas teniendo lugar de sobra. El documento con
    // `setTextWidth(-1)` da el ancho real de UNA linea, que es la unica medida que sirve para
    // decidir si hace falta envolver.
    const QMargins margins = m_contentLayout->contentsMargins();
    const int chromeW = margins.left() + margins.right() + 2 * kLabelPadding;
    const int chromeH = margins.top() + margins.bottom() + 2 * kLabelPadding;

    QTextDocument doc;
    doc.setDefaultFont(m_messageLabel->font());   // ANTES del setHtml
    doc.setDocumentMargin(0);
    doc.setHtml(m_messageLabel->text());
    doc.setTextWidth(-1);

    const int calculatedWidth =
        qBound(kMinWidth, qCeil(doc.idealWidth()) + chromeW + kWidthSlack, kMaxWidth);

    // El alto se mide DESPUES, ya con el ancho resuelto: si el texto no entraba en una linea,
    // recien aca se sabe en cuantas quedo.
    doc.setTextWidth(calculatedWidth - chromeW);
    const int textHeight = qMax(kMinTextHeight, qCeil(doc.size().height()));
    const int calculatedHeight = textHeight + chromeH;

    m_contentWidget->setFixedSize(calculatedWidth, calculatedHeight);
    QWidget::adjustSize();
    layoutOverlays();
}

void MessagePopover::showPopover(int autoCloseMs)
{
    m_autoCloseMs = qMax(0, autoCloseMs);

    updateContentSize();

    // Se muestra invisible primero para que el tamaño quede resuelto (el `showEvent` re-mide
    // con la fuente ya aplicada por el QSS), y recien ahi se centra.
    setWindowOpacity(0.0);
    show();

    const QSize popoverSize = size();
    QRect area;
    if (parentWidget()) {
        area = parentWidget()->window()->frameGeometry();
    } else if (QScreen *screen = QGuiApplication::primaryScreen()) {
        area = screen->availableGeometry();
    }
    move(area.center().x() - popoverSize.width() / 2,
         area.center().y() - popoverSize.height() / 2);
    setWindowOpacity(1.0);

    startAutoClose();
}

void MessagePopover::startAutoClose()
{
    // Cortar la cuenta anterior ANTES de armar otra. Sin esto, dos `showPopover()` seguidos
    // dejaban DOS animaciones vivas conectadas a `close()`: la pausa al hover frenaba una sola
    // y la otra cerraba el popover con el mouse encima.
    if (m_autoCloseAnim) {
        m_autoCloseAnim->stop();
        m_autoCloseAnim->deleteLater();
        m_autoCloseAnim = nullptr;
    }

    if (m_autoCloseMs <= 0) {
        if (m_rail) m_rail->hide();
        return;
    }

    // El riel es el ADORNO; el cierre es la funcion. Si por lo que sea no hay riel, la cuenta
    // corre igual: un `return` temprano aca dejaria el popover abierto para siempre.
    if (m_rail) {
        m_rail->setFraction(1.0);
        m_rail->attachToBottom();
        m_rail->show();
    }

    // Una sola fuente de tiempo para las dos cosas: la animacion ES la cuenta regresiva y
    // ademas dibuja el riel. Con un QTimer aparte para cerrar, la pausa al hover tendria que
    // sincronizar dos relojes y se desfasarian.
    m_autoCloseAnim = new QVariantAnimation(this);
    m_autoCloseAnim->setStartValue(1.0);
    m_autoCloseAnim->setEndValue(0.0);
    m_autoCloseAnim->setDuration(m_autoCloseMs);
    m_autoCloseAnim->setEasingCurve(QEasingCurve::Linear);
    connect(m_autoCloseAnim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) {
                if (m_rail) m_rail->setFraction(value.toReal());
            });
    connect(m_autoCloseAnim, &QVariantAnimation::finished, this, &MessagePopover::close);
    m_autoCloseAnim->start();
}

void MessagePopover::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    // Un aviso que se va justo mientras lo estas leyendo es peor que uno que se queda.
    if (m_autoCloseAnim && m_autoCloseAnim->state() == QAbstractAnimation::Running) {
        m_autoCloseAnim->pause();
    }
}

void MessagePopover::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    resumeAutoClose();
}

bool MessagePopover::event(QEvent *event)
{
    // Red de seguridad: `leaveEvent` NO alcanza como unico camino de vuelta. Si la app se
    // desactiva con el mouse encima del popover, o si el popover se esconde antes de que
    // llegue el Leave, la cuenta quedaba en `Paused` PARA SIEMPRE, y como la ventana es
    // `WindowStaysOnTopHint` quedaba un aviso siempre-arriba que ya no se cerraba solo.
    if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Hide) {
        resumeAutoClose();
    }
    return QWidget::event(event);
}

void MessagePopover::resumeAutoClose()
{
    if (m_autoCloseAnim && m_autoCloseAnim->state() == QAbstractAnimation::Paused) {
        m_autoCloseAnim->resume();
    }
}

void MessagePopover::closePopover()
{
    // Parar la cuenta al cerrar por afuera. `stop()` a mitad de camino NO emite `finished`,
    // asi que no reentra en `close()`.
    if (m_autoCloseAnim) {
        m_autoCloseAnim->stop();
    }
    close();
}

bool MessagePopover::isInDragArea(const QPoint& point) const
{
    // Toda la ventana es arrastrable, excepto el boton de cierre. Ojo si se hace el texto
    // seleccionable: un arrastre para seleccionar tambien moveria el popover.
    if (m_closeButton && m_closeButton->rect().contains(m_closeButton->mapFrom(this, point))) {
        return false;
    }
    return true;
}

void MessagePopover::mousePressEvent(QMouseEvent *event)
{
    // El arrastre va primero A PROPOSITO: el margen de la sombra tambien arrastra, no cierra.
    // Un aviso de error no tiene que perderse por un click que cayo apenas afuera de la tarjeta.
    if (event->button() == Qt::LeftButton && isInDragArea(event->pos())) {
        m_isDragging = true;
        m_dragStartPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void MessagePopover::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragStartPosition);
        event->accept();
    } else {
        QWidget::mouseMoveEvent(event);
    }
}

void MessagePopover::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_isDragging && event->button() == Qt::LeftButton) {
        m_isDragging = false;
        event->accept();
    } else {
        QWidget::mouseReleaseEvent(event);
    }
}

void MessagePopover::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        event->accept();
        closePopover();
    } else {
        QWidget::keyPressEvent(event);
    }
}

QLabel* MessagePopover::messageLabel() const
{
    return m_messageLabel;
}

void MessagePopover::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Re-medir ya con el QSS aplicado: antes del primer show el label puede no estar pulido
    // y medir con la fuente por defecto.
    updateContentSize();
}

void MessagePopover::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    if (!m_contentWidget) {
        return;
    }

    // La sombra se dibuja aca y no con un QGraphicsDropShadowEffect sobre la tarjeta: un
    // efecto sobre un QWidget no cachea nada, y el riel de auto-cierre repinta a ~60 Hz, asi
    // que cada frame re-renderizaba y re-blureaba el popover entero en el hilo de UI.
    // `paintRoundedShadow()` arma la sombra una vez y la cachea; cada frame cuesta un
    // `drawPixmap`.
    //
    // El pixmap incluye, ademas del halo, el rectangulo redondeado NEGRO que lo proyecta. Lo
    // tapa `m_contentWidget`, que se pinta despues por ser hijo y lleva su fondo opaco del QSS.
    // Por eso el radio que se pasa aca tiene que ser el `border-radius` del QSS.
    //
    // No dibujar una segunda sombra a mano encima (rectangulos concentricos de alpha
    // decreciente): queda escalonada. Si hace falta mas peso, se sube
    // `PopoverShadow::kOpacity`.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    PopoverShadow::paintRoundedShadow(painter, QRectF(m_contentWidget->geometry()), kContentRadius);
}
