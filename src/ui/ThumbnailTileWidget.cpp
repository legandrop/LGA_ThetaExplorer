#include "thetaexplorer/ThumbnailTileWidget.h"
#include "thetaexplorer/ColorUtils.h"
#include "thetaexplorer/UiIcons.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QFileInfo>
#include <QDateTime>
#include <QStyle>
#include <QLocale>

namespace {
// Colores que se pintan por codigo (glyphs y el fondo del tile); el resto va por QSS.
constexpr auto kBadgeSavedText   = "#eaf7ea";
constexpr auto kBadgePartialText = "#fff3d6";
constexpr auto kVideoColor       = "#8ab4ff";
constexpr auto kDateOldColor     = "#8f8f8f";
constexpr auto kDateFutureColor  = "#8ae88f";
constexpr auto kTileBg           = "#1d1d1d";
constexpr auto kTileBgHover      = "#222222";
constexpr auto kTileBgSelected   = "#2a2040";
constexpr auto kTileBorder       = "#2e2e2e";
constexpr auto kTileBorderHover  = "#555555";
}

ThumbnailTileWidget::ThumbnailTileWidget(const MediaAssetGroup& group, QWidget* parent)
    : QWidget(parent)
    , m_group(group)
{
    setFixedSize(WIDTH, HEIGHT);
    setObjectName("thumbnailTile");
    setCursor(Qt::PointingHandCursor);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 0);
    layout->setSpacing(6);

    // Area de miniatura.
    m_thumb = new QLabel(this);
    const int contentWidth = WIDTH - 12;
    m_thumb->setFixedSize(contentWidth, THUMBNAIL_HEIGHT);
    m_thumb->setAlignment(Qt::AlignCenter);
    m_thumb->setObjectName("tileThumb");

    // Historial confirmado: independiente de que las copias sigan en el destino.
    const qreal dpr = devicePixelRatioF();
    m_downloadedBadge = new QLabel(m_thumb);
    m_downloadedBadge->setObjectName("tileBadge");
    m_downloadedBadge->setTextFormat(Qt::RichText);
    if (group.allFilesDownloaded || group.hasPartialDownloadHistory) {
        const bool full = group.allFilesDownloaded;
        m_downloadedBadge->setProperty("state", full ? "saved" : "partial");
        auto* badgeLayout = new QHBoxLayout(m_downloadedBadge);
        badgeLayout->setContentsMargins(5, 0, 6, 0);
        badgeLayout->setSpacing(3);
        auto* badgeIcon = new QLabel(m_downloadedBadge);
        badgeIcon->setPixmap(UiIcons::pixmap(full ? "check" : "partial", 12,
                                             QColor(full ? kBadgeSavedText : kBadgePartialText), dpr));
        auto* badgeText = new QLabel(full ? "Downloaded" : "Partial", m_downloadedBadge);
        badgeText->setObjectName("tileBadgeText");
        badgeText->setProperty("state", full ? "saved" : "partial");
        badgeLayout->addWidget(badgeIcon);
        badgeLayout->addWidget(badgeText);
        // Polish antes de medir: la fuente chica del texto sale del QSS.
        badgeText->ensurePolished();
        m_downloadedBadge->ensurePolished();
        // El tamano sale del layout: QLabel::sizeHint() ignora el layout de sus hijos, asi
        // que adjustSize() lo dejaba en 8 px de ancho.
        m_downloadedBadge->setFixedSize(badgeLayout->sizeHint().width(), 22);
        m_downloadedBadge->move(m_thumb->width() - m_downloadedBadge->width() - 6, 6);
        m_downloadedBadge->setVisible(true);
    } else {
        m_downloadedBadge->setVisible(false);
    }

    // Placeholder hasta que llega el thumbnail
    if (group.isVideo) {
        m_thumb->setPixmap(UiIcons::pixmap("play", 30, QColor(kVideoColor), dpr));
    } else if (group.isRaw) {
        m_thumb->setText("RAW");
        m_thumb->setProperty("placeholder", "raw");
    }

    m_formatBadge = new QLabel(m_thumb);
    m_formatBadge->setObjectName("tileFormat");
    m_formatBadge->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto* footer = new QWidget(this);
    footer->setObjectName("tileFooter");
    footer->setFixedHeight(HEIGHT - FOOTER_TOP);
    auto* footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(0, 4, 0, 6);
    footerLayout->setSpacing(2);

    m_nameLabel = new QLabel(footer);
    m_nameLabel->setObjectName("tileName");
    m_nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_nameLabel->setWordWrap(false);
    m_nameLabel->setFixedWidth(contentWidth);
    m_nameLabel->setFixedHeight(18);

    // Recortar nombres largos por el centro.
    m_nameLabel->ensurePolished();
    QFontMetrics fm(m_nameLabel->font());
    m_nameLabel->setText(fm.elidedText(group.displayTitle, Qt::ElideMiddle, contentWidth));

    m_datesLabel = new QLabel(footer);
    m_datesLabel->setObjectName("tileDate");
    m_datesLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_datesLabel->setFixedWidth(contentWidth);
    m_datesLabel->setFixedHeight(18);
    m_datesLabel->setTextFormat(Qt::RichText);
    m_datesLabel->setText(buildDateHtml());

    layout->addWidget(m_thumb);
    footerLayout->addWidget(m_nameLabel);
    footerLayout->addWidget(m_datesLabel);
    layout->addWidget(footer);

    const QString formats = group.isCombinedHdr() ? "DNG + JPG" : group.isRaw ? "DNG" : "JPG";
    m_formatBadge->setText(group.isHdrSet
        ? QString("HDRI %1 · %2").arg(formats).arg(group.bracketCount()) : group.subtitle);
    m_formatBadge->ensurePolished();
    m_formatBadge->adjustSize();
    m_formatBadge->move(5, m_thumb->height() - m_formatBadge->height() - 5);
    setAccessibleName(QString("%1, %2, %3").arg(group.displayTitle, group.subtitle,
        group.captureTime.toString("yyyy-MM-dd HH:mm")));
    updateStyleState();
}

QString ThumbnailTileWidget::buildDateHtml() const
{
    const QDateTime timestamp = m_group.representative.creationDate;
    const QString color = ageColor(timestamp);
    if (!timestamp.isValid()) {
        return QString("<span style=\"color:%1;\">no-date</span>").arg(color);
    }

    const QString dayLine = QLocale(QLocale::English).toString(timestamp, "ddd d MMM");
    const QString timeLine = timestamp.toString("HH:mm");
    return QString("<span style=\"color:%1;\">%2 &middot; %3</span>")
        .arg(color, dayLine, timeLine);
}

QString ThumbnailTileWidget::ageColor(const QDateTime& timestamp) const
{
    if (!timestamp.isValid()) {
        return kDateOldColor;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const qint64 ageSecs = timestamp.secsTo(now);
    if (ageSecs < 0) {
        return kDateFutureColor;
    }

    const double ageHours = ageSecs / 3600.0;
    if (ageHours > 28.0) {
        return kDateOldColor;
    }

    const double t = qBound(0.0, ageHours / 28.0, 1.0);
    const bool raw = m_group.isRaw;
    const int startR = raw ? 255 : 192;
    const int startG = raw ? 220 : 255;
    const int startB = raw ? 150 : 192;
    const int endR = raw ? 156 : 35;
    const int endG = raw ? 103 : 102;
    const int endB = raw ? 35 : 52;
    const int r = static_cast<int>(startR + (endR - startR) * t);
    const int g = static_cast<int>(startG + (endG - startG) * t);
    const int b = static_cast<int>(startB + (endB - startB) * t);
    return QString("#%1%2%3")
        .arg(r, 2, 16, QChar('0'))
        .arg(g, 2, 16, QChar('0'))
        .arg(b, 2, 16, QChar('0'));
}

void ThumbnailTileWidget::setThumbnail(const QPixmap& pixmap)
{
    if (pixmap.isNull()) return;
    QPixmap scaled = pixmap.scaled(
        m_thumb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation
    );
    m_thumb->setPixmap(scaled);
}

void ThumbnailTileWidget::setSelected(bool s)
{
    if (m_selected == s) return;
    m_selected = s;
    updateStyleState();
    update();
}

void ThumbnailTileWidget::updateStyleState()
{
    // Propiedades dinamicas para las reglas QSS de estado.
    setProperty("selected", m_selected);
    setProperty("hovered",  m_hovered);
    style()->unpolish(this);
    style()->polish(this);
}

void ThumbnailTileWidget::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QColor bg    = m_selected ? QColor(kTileBgSelected) : (m_hovered ? QColor(kTileBgHover) : QColor(kTileBg));
    QColor border = m_selected ? QColor(ColorUtils::VIOLETA_CLARO)
        : m_hovered ? QColor(kTileBorderHover) : QColor(kTileBorder);
    int bw = m_selected ? 2 : 1;

    p.setPen(QPen(border, bw));
    p.setBrush(bg);
    p.drawRoundedRect(rect().adjusted(bw/2, bw/2, -bw/2, -bw/2), 6, 6);
}

void ThumbnailTileWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        emit clicked(this, e->modifiers());
    }
    QWidget::mousePressEvent(e);
}

void ThumbnailTileWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        emit doubleClicked(this);
    }
    QWidget::mouseDoubleClickEvent(e);
}

void ThumbnailTileWidget::enterEvent(QEnterEvent* e)
{
    m_hovered = true;
    updateStyleState();
    update();
    QWidget::enterEvent(e);
}

void ThumbnailTileWidget::leaveEvent(QEvent* e)
{
    m_hovered = false;
    updateStyleState();
    update();
    QWidget::leaveEvent(e);
}
