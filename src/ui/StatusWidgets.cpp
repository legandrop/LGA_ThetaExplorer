#include "thetaexplorer/StatusWidgets.h"
#include "thetaexplorer/ColorUtils.h"
#include "thetaexplorer/UiIcons.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr auto kDotOffColor    = "#6a6a6a";
constexpr auto kEmptyIconColor = "#5a5a5a";
constexpr int  kEmptyIconSize  = 64;
}

// ---------------------------------------------------------------- StatusDot

StatusDot::StatusDot(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(sizeHint());
    setAttribute(Qt::WA_TranslucentBackground);
    m_timer = new QTimer(this);
    m_timer->setInterval(30);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        m_angle = (m_angle + 12) % 360;
        update();
    });
}

void StatusDot::setMode(Mode mode)
{
    if (m_mode == mode) return;
    m_mode = mode;
    if (m_mode == Mode::Searching) m_timer->start();
    else m_timer->stop();
    update();
}

void StatusDot::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c = QRectF(rect()).center();

    switch (m_mode) {
        case Mode::Connected: {
            // Halo suave + punto, como en la maqueta.
            QColor halo(ColorUtils::SUCCESS);
            halo.setAlphaF(0.18);
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            p.drawEllipse(c, 7.0, 7.0);
            p.setBrush(QColor(ColorUtils::SUCCESS));
            p.drawEllipse(c, 4.0, 4.0);
            break;
        }
        case Mode::Off:
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(kDotOffColor));
            p.drawEllipse(c, 4.0, 4.0);
            break;
        case Mode::Searching: {
            // Arco que gira: tres cuartos de circunferencia.
            QPen pen(QColor(ColorUtils::TXT_SECUNDARIO), 1.6);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            const QRectF r(c.x() - 5, c.y() - 5, 10, 10);
            p.drawArc(r, -m_angle * 16, 270 * 16);
            break;
        }
    }
}

// ---------------------------------------------------------------- EmptyStateWidget

EmptyStateWidget::EmptyStateWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("emptyState");
    setAttribute(Qt::WA_StyledBackground);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(40, 40, 40, 40);
    outer->addStretch(1);

    auto* icon = new QLabel(this);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(UiIcons::pixmap("camera_theta", kEmptyIconSize, QColor(kEmptyIconColor),
                                    devicePixelRatioF()));
    outer->addWidget(icon);
    outer->addSpacing(14);

    m_title = new QLabel(this);
    m_title->setObjectName("emptyStateTitle");
    m_title->setAlignment(Qt::AlignCenter);
    outer->addWidget(m_title);
    outer->addSpacing(6);

    m_text = new QLabel(this);
    m_text->setObjectName("emptyStateText");
    m_text->setAlignment(Qt::AlignCenter);
    m_text->setWordWrap(true);
    m_text->setFixedWidth(320);
    // Centrado con stretches y SIN alineacion en el layout: un QLabel con wordWrap alineado
    // pierde su heightForWidth y corta la segunda linea (ver hub Qt/C++).
    auto* textRow = new QHBoxLayout();
    textRow->addStretch(1);
    textRow->addWidget(m_text);
    textRow->addStretch(1);
    outer->addLayout(textRow);

    m_searchRow = new QWidget(this);
    auto* searchLayout = new QHBoxLayout(m_searchRow);
    searchLayout->setContentsMargins(0, 18, 0, 0);
    searchLayout->setSpacing(8);
    m_searchDot = new StatusDot(m_searchRow);
    auto* searchText = new QLabel("Searching for camera…", m_searchRow);
    searchText->setObjectName("emptyStateSearching");
    searchLayout->addWidget(m_searchDot, 0, Qt::AlignVCenter);
    searchLayout->addWidget(searchText, 0, Qt::AlignVCenter);
    outer->addWidget(m_searchRow, 0, Qt::AlignHCenter);

    outer->addStretch(1);
}

void EmptyStateWidget::setContent(const QString& title, const QString& text, bool searching)
{
    m_title->setText(title);
    m_text->setText(text);
    m_searchRow->setVisible(searching);
    m_searchDot->setMode(searching ? StatusDot::Mode::Searching : StatusDot::Mode::Off);
}
