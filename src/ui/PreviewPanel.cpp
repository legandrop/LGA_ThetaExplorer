#include "thetaexplorer/PreviewPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QLabel>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>
#include "thetaexplorer/UiIcons.h"

namespace {
constexpr auto kVideoColor = "#8ab4ff";   // mismo azul que la etiqueta VIDEO del tile
}

PreviewPanel::PreviewPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("previewPanel");
    setAttribute(Qt::WA_StyledBackground);
    setMinimumWidth(200);

    m_stack = new QStackedWidget(this);

    // Dos áreas iguales: los QLabel ignoran el tamaño nativo al repartir el espacio.
    auto* imagePage = new QWidget(this);
    auto* imagesLayout = new QVBoxLayout(imagePage);
    imagesLayout->setContentsMargins(0, 0, 0, 0);
    imagesLayout->setSpacing(12);
    for (int section = 0; section < 2; ++section) {
        auto* area = new QWidget(imagePage);
        auto* areaLayout = new QVBoxLayout(area);
        areaLayout->setContentsMargins(0, 0, 0, 0);
        areaLayout->setSpacing(6);
        auto* header = new QHBoxLayout;
        auto* title = new QLabel(section == 0 ? "Original" : "Stitched preview", area);
        title->setObjectName("previewSectionHeader");
        auto* detail = new QLabel(area);
        detail->setObjectName(section == 0 ? "previewBracketLabel" : "previewApproximateLabel");
        header->addWidget(title);
        header->addStretch();
        header->addWidget(detail);
        areaLayout->addLayout(header);
        auto* image = new QLabel(area);
        image->setAlignment(Qt::AlignCenter);
        image->setTextFormat(Qt::PlainText);
        image->setWordWrap(true);
        image->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        image->setMinimumSize(0, 0);
        image->setObjectName(section == 0 ? "previewImage" : "stitchedPreviewImage");
        areaLayout->addWidget(image, 1);
        imagesLayout->addWidget(area, 1);
        if (section == 0) {
            m_imageLabel = image;
            m_originalTitle = title;
            m_bracketLabel = detail;
        } else {
            m_stitchedLabel = image;
            m_approximateLabel = detail;
        }
    }

    // Video sin inventar una panorámica.
    m_videoLabel = new QLabel(this);
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setPixmap(UiIcons::pixmap("play", 48, QColor(kVideoColor), devicePixelRatioF()));

    // Sin selección.
    m_emptyLabel = new QLabel("No item selected", this);
    m_emptyLabel->setObjectName("previewPlaceholder");
    m_emptyLabel->setAlignment(Qt::AlignCenter);

    // Selección múltiple.
    m_multiLabel = new QLabel(this);
    m_multiLabel->setObjectName("previewPlaceholder");
    m_multiLabel->setAlignment(Qt::AlignCenter);

    m_stack->addWidget(imagePage);     // 0
    m_stack->addWidget(m_videoLabel);  // 1
    m_stack->addWidget(m_emptyLabel);  // 2
    m_stack->addWidget(m_multiLabel);  // 3
    m_stack->setCurrentIndex(2);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->addWidget(m_stack);

    m_watcher = new QFutureWatcher<DualFisheyePreview::Result>(this);
    connect(m_watcher, &QFutureWatcher<DualFisheyePreview::Result>::finished, this, [this] {
        const auto result = m_watcher->result();
        const bool current = m_runningGeneration == m_generation
                             && m_runningGroupId == m_currentGroup.id && m_showingImage;
        QPointer<PreviewPanel> alive(this);
        if (current) {
            if (result.error.isEmpty()) {
                m_cache.prepend({m_runningGroupId, m_runningFilePath, result});
                if (m_cache.size() > 8) m_cache.removeLast();
            }
            applyPreview(result);
        }
        if (!alive) return;
        m_workerActive = false;
        m_runningGroupId.clear();
        m_runningFilePath.clear();
        startPendingPreview();
    });
}

void PreviewPanel::showGroup(const MediaAssetGroup& group, const QPixmap& thumbnail)
{
    const bool changed = group.id != m_currentGroup.id
                         || group.representativeDevicePath() != m_currentGroup.representativeDevicePath();
    if (changed) {
        invalidatePreview();
        m_currentGroup = group;
        m_originalTitle->setText("Original");
        const int brackets = group.bracketCount();
        m_bracketLabel->setText(group.isHdrSet && brackets > 0
            ? QString("Bracket %1 of %2").arg(group.representativeIndex % brackets + 1).arg(brackets)
            : QString());
        m_approximateLabel->clear();
        m_imageLabel->setText("Loading preview…");
        const bool rawOnly = group.kind == MediaAssetKind::SingleRaw || group.kind == MediaAssetKind::HdrRawSet;
        m_stitchedLabel->setText(rawOnly ? "Stitched preview is unavailable for RAW-only items."
                                        : "Loading stitched preview…");
    } else {
        m_currentGroup = group;
    }
    if (group.isVideo) {
        m_showingImage = false;
        m_stack->setCurrentIndex(1);
    } else {
        m_showingImage = true;
        m_stack->setCurrentIndex(0);
        // Una miniatura tardía del mismo grupo no reemplaza el JPEG ya recibido.
        if (!thumbnail.isNull() && !m_hasFullPreview) m_originalPixmap = thumbnail;
        updateScaledImage();
    }
}

void PreviewPanel::showPreviewFile(const MediaAssetGroup& group, const QString& filePath)
{
    if (group.id != m_currentGroup.id || !m_showingImage || filePath.isEmpty()
        || group.kind == MediaAssetKind::SingleRaw || group.kind == MediaAssetKind::HdrRawSet) return;
    for (int i = 0; i < m_cache.size(); ++i) {
        if (m_cache[i].groupId != group.id || m_cache[i].filePath != filePath) continue;
        const auto cached = m_cache.takeAt(i);
        m_cache.prepend(cached);
        ++m_generation;
        m_pendingFilePath.clear();
        applyPreview(cached.result);
        return;
    }
    if (m_runningGeneration == m_generation && m_runningGroupId == group.id
        && m_runningFilePath == filePath) return;
    ++m_generation;
    m_pendingFilePath = filePath;
    m_stitchedPixmap = {};
    m_stitchedLabel->setText("Loading stitched preview…");
    startPendingPreview();
}

void PreviewPanel::showPreviewError(const QString& devicePath, const QString& message)
{
    if (!m_showingImage || m_currentGroup.id.isEmpty()) return;
    bool belongs = devicePath == m_currentGroup.representativeDevicePath();
    for (const auto& file : m_currentGroup.files) belongs |= file.devicePath == devicePath;
    if (!belongs) return;
    ++m_generation;
    m_pendingFilePath.clear();
    m_stitchedPixmap = {};
    m_approximateLabel->clear();
    if (m_originalPixmap.isNull()) m_imageLabel->setText("Preview unavailable");
    m_stitchedLabel->setText(message);
}

void PreviewPanel::startPendingPreview()
{
    if (m_workerActive || m_pendingFilePath.isEmpty()) return;
    const QString filePath = m_pendingFilePath;
    m_pendingFilePath.clear();
    m_runningGroupId = m_currentGroup.id;
    m_runningFilePath = filePath;
    m_runningGeneration = m_generation;
    m_workerActive = true;
    // El worker captura sólo valores; destruir el panel desconecta su watcher sin tocar la imagen.
    m_watcher->setFuture(QtConcurrent::run([filePath] { return DualFisheyePreview::read(filePath); }));
}

void PreviewPanel::applyPreview(const DualFisheyePreview::Result& result)
{
    using Projection = DualFisheyePreview::Projection;
    if (!result.original.isNull()) {
        m_originalPixmap = QPixmap::fromImage(result.original);
        m_hasFullPreview = true;
    }
    m_stitchedPixmap = QPixmap::fromImage(result.stitched);
    const bool dual = result.projection == Projection::DualFisheye
                      || result.projection == Projection::DualFisheyeUpright;
    m_originalTitle->setText(dual ? "Dual lens" : "Original");
    m_approximateLabel->setText(dual && !result.stitched.isNull() ? "Approximate"
        : result.projection == Projection::Equirectangular ? "Already stitched" : "");
    if (!result.error.isEmpty()) {
        if (m_originalPixmap.isNull()) m_imageLabel->setText("Preview unavailable");
        m_stitchedLabel->setText(result.error);
    } else if (result.stitched.isNull()) {
        m_stitchedLabel->setText("Stitched preview is unavailable for this image projection.");
    }
    updateScaledImage();
    if (!result.error.isEmpty()) emit previewFailed(result.error);
}

void PreviewPanel::invalidatePreview()
{
    ++m_generation;
    m_pendingFilePath.clear();
    m_hasFullPreview = false;
    m_originalPixmap = {};
    m_stitchedPixmap = {};
    m_imageLabel->clear();
    m_stitchedLabel->clear();
}

void PreviewPanel::showMultipleSelection(int count)
{
    invalidatePreview();
    m_currentGroup = {};
    m_showingImage = false;
    m_multiLabel->setText(QString("%1 items selected").arg(count));
    m_stack->setCurrentIndex(3);
}

void PreviewPanel::clearPreview()
{
    invalidatePreview();
    m_currentGroup = {};
    m_cache.clear();
    m_showingImage = false;
    m_stack->setCurrentIndex(2);
}

void PreviewPanel::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (m_showingImage) updateScaledImage();
}

void PreviewPanel::updateScaledImage()
{
    // El layout se activa también antes de mostrar el widget, para evitar el tamaño inicial 100x30.
    if (!m_showingImage) return;
    layout()->activate();
    m_stack->layout()->activate();
    m_stack->widget(0)->layout()->activate();
    m_imageLabel->parentWidget()->layout()->activate();
    m_stitchedLabel->parentWidget()->layout()->activate();
    for (QLabel* label : {m_imageLabel, m_stitchedLabel}) {
        const QPixmap& original = label == m_imageLabel ? m_originalPixmap : m_stitchedPixmap;
        if (original.isNull()) continue;
        label->setPixmap(original.scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}
