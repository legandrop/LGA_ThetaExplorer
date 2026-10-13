#include "thetaexplorer/ThumbnailGridWidget.h"
#include "thetaexplorer/ThumbnailTileWidget.h"
#include <QResizeEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QScrollBar>

ThumbnailGridWidget::ThumbnailGridWidget(QWidget* parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Reservar el gutter evita que aparezca/desaparezca una columna con el scroll.
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setObjectName("thumbnailGrid");
    setFocusPolicy(Qt::StrongFocus);

    m_container = new QWidget(this);
    m_container->setObjectName("gridContainer");
    m_container->setAttribute(Qt::WA_StyledBackground);

    m_layout = new QGridLayout(m_container);
    m_layout->setContentsMargins(LEFT_MARGIN, 8, RIGHT_MARGIN, 8);
    m_layout->setSpacing(SPACING);
    m_layout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    setWidget(m_container);
    ensurePolished();
    setMinimumWidth(snappedWidth(0, QWIDGETSIZE_MAX));
}

void ThumbnailGridWidget::setGroups(const QList<MediaAssetGroup>& groups)
{
    clearAll();
    QList<CameraFileInfo> thumbnailRequests;
    for (const MediaAssetGroup& group : groups) {
        auto* tile = new ThumbnailTileWidget(group, m_container);
        connect(tile, &ThumbnailTileWidget::clicked,
                this, &ThumbnailGridWidget::onTileClicked);
        m_tiles.append(tile);
        const QString key = group.representativeDevicePath();
        if (m_thumbnailCache.contains(key)) {
            tile->setThumbnail(m_thumbnailCache.value(key));
        } else {
            thumbnailRequests.append(group.representative);
        }
    }
    relayoutGrid();
    if (!thumbnailRequests.isEmpty()) {
        emit thumbnailsNeeded(thumbnailRequests);
    }
}

void ThumbnailGridWidget::setThumbnail(const QString& devicePath, const QPixmap& thumbnail)
{
    if (!thumbnail.isNull()) {
        m_thumbnailCache.insert(devicePath, thumbnail);
    }
    for (ThumbnailTileWidget* tile : m_tiles) {
        if (tile->group().representativeDevicePath() == devicePath) {
            tile->setThumbnail(thumbnail);
            return;
        }
    }
}

void ThumbnailGridWidget::removeFiles(const QStringList& devicePaths)
{
    QList<ThumbnailTileWidget*> toRemove;
    for (ThumbnailTileWidget* tile : m_tiles) {
        for (const CameraFileInfo& file : tile->group().files) {
            if (devicePaths.contains(file.devicePath)) {
                toRemove.append(tile);
                break;
            }
        }
    }
    for (ThumbnailTileWidget* tile : toRemove) {
        if (m_lastClicked == tile) m_lastClicked = nullptr;
        if (m_currentTile == tile) m_currentTile = nullptr;
        m_tiles.removeOne(tile);
        tile->setParent(nullptr);
        tile->deleteLater();
    }
    relayoutGrid();
    emit selectionChanged(selectedGroups());
}

void ThumbnailGridWidget::clearAll()
{
    for (ThumbnailTileWidget* tile : m_tiles) {
        tile->setParent(nullptr);
        tile->deleteLater();
    }
    m_tiles.clear();
    m_lastClicked = nullptr;
    m_currentTile = nullptr;
    emit selectionChanged({});
}

QList<MediaAssetGroup> ThumbnailGridWidget::selectedGroups() const
{
    QList<MediaAssetGroup> result;
    for (ThumbnailTileWidget* tile : m_tiles) {
        if (tile->isSelected()) result.append(tile->group());
    }
    return result;
}

void ThumbnailGridWidget::onTileClicked(ThumbnailTileWidget* tile, Qt::KeyboardModifiers mods)
{
    setFocus(Qt::MouseFocusReason);
    selectTile(tile, mods);
}

void ThumbnailGridWidget::selectTile(ThumbnailTileWidget* tile, Qt::KeyboardModifiers mods)
{
    bool ctrl  = mods & Qt::ControlModifier;
    bool shift = mods & Qt::ShiftModifier;

    if (shift && m_lastClicked) {
        // El ancla se conserva al extender o reducir el rango.
        int a = m_tiles.indexOf(m_lastClicked);
        int b = m_tiles.indexOf(tile);
        if (a > b) qSwap(a, b);
        for (int i = 0; i < m_tiles.size(); ++i) {
            m_tiles[i]->setSelected(i >= a && i <= b);
        }
    } else if (ctrl) {
        // Alternar una tarjeta sin reemplazar las demas.
        tile->setSelected(!tile->isSelected());
        m_lastClicked = tile;
    } else {
        // Seleccion unica.
        for (ThumbnailTileWidget* t : m_tiles) t->setSelected(false);
        tile->setSelected(true);
        m_lastClicked = tile;
    }
    m_currentTile = tile;
    emit selectionChanged(selectedGroups());
}

void ThumbnailGridWidget::relayoutGrid()
{
    // Sacar los wrappers del layout sin borrar las tarjetas.
    while (m_layout->count()) {
        delete m_layout->takeAt(0);
    }
    const int cols = columnCount();
    for (int i = 0; i < m_tiles.size(); ++i) {
        m_layout->addWidget(m_tiles[i], i / cols, i % cols, Qt::AlignTop);
        m_tiles[i]->setVisible(true);
    }

    // El alto se mide con la misma tarjeta que se presenta.
    int rows = (m_tiles.size() + cols - 1) / cols;
    int h = qMax(0, rows - 1) * SPACING + 16;
    for (int row = 0; row < rows; ++row) {
        int rowHeight = 0;
        for (int i = row * cols; i < qMin((row + 1) * cols, int(m_tiles.size())); ++i) {
            rowHeight = qMax(rowHeight, m_tiles[i]->height());
        }
        h += rowHeight;
    }
    m_container->setMinimumHeight(h);
}

int ThumbnailGridWidget::columnCount() const
{
    int availableW = viewport()->width() - LEFT_MARGIN - RIGHT_MARGIN;
    int cols = qMax(1, (availableW + SPACING) / (ThumbnailTileWidget::WIDTH + SPACING));
    return cols;
}

int ThumbnailGridWidget::snappedWidth(int requestedWidth, int maximumWidth) const
{
    verticalScrollBar()->ensurePolished();
    const int chrome = LEFT_MARGIN + RIGHT_MARGIN + 2 * frameWidth()
                       + verticalScrollBar()->sizeHint().width();
    const int stride = ThumbnailTileWidget::WIDTH + SPACING;
    const int maxColumns = qMax(1, (maximumWidth - chrome + SPACING) / stride);
    const int columns = qBound(1, qRound(double(requestedWidth - chrome + SPACING) / stride),
                               maxColumns);
    return qMin(maximumWidth, columns * stride - SPACING + chrome);
}

void ThumbnailGridWidget::resizeEvent(QResizeEvent* e)
{
    QScrollArea::resizeEvent(e);
    relayoutGrid();
}

void ThumbnailGridWidget::keyPressEvent(QKeyEvent* e)
{
    if (e->matches(QKeySequence::SelectAll)) {
        for (ThumbnailTileWidget* t : m_tiles) t->setSelected(true);
        if (!m_currentTile && !m_tiles.isEmpty()) m_currentTile = m_tiles.first();
        if (!m_lastClicked) m_lastClicked = m_currentTile;
        emit selectionChanged(selectedGroups());
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        for (ThumbnailTileWidget* t : m_tiles) t->setSelected(false);
        m_lastClicked = nullptr;
        m_currentTile = nullptr;
        emit selectionChanged(selectedGroups());
        e->accept();
        return;
    }
    if (e->key() != Qt::Key_Left && e->key() != Qt::Key_Right &&
        e->key() != Qt::Key_Up && e->key() != Qt::Key_Down) {
        QScrollArea::keyPressEvent(e);
        return;
    }

    e->accept();
    if (m_tiles.isEmpty()) return;

    int current = m_tiles.indexOf(m_currentTile);
    if (current < 0) {
        for (int i = 0; i < m_tiles.size(); ++i) {
            if (m_tiles[i]->isSelected()) {
                current = i;
                break;
            }
        }
    }

    int target = 0;
    if (current >= 0) {
        target = current;
        const int cols = columnCount();
        const int last = static_cast<int>(m_tiles.size()) - 1;
        switch (e->key()) {
        case Qt::Key_Left:  target = qMax(0, current - 1); break;
        case Qt::Key_Right: target = qMin(last, current + 1); break;
        case Qt::Key_Up:
            if (current >= cols) target = current - cols;
            break;
        case Qt::Key_Down:
            if (current / cols < last / cols) target = qMin(last, current + cols);
            break;
        }
    }

    ThumbnailTileWidget* tile = m_tiles[target];
    selectTile(tile, e->modifiers() & Qt::ShiftModifier);
    ensureWidgetVisible(tile, 0, 0);
}

void ThumbnailGridWidget::mousePressEvent(QMouseEvent* e)
{
    // El evento usa coordenadas del viewport; el contenedor incluye el desplazamiento.
    if (e->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        QWidget* child = m_container->childAt(m_container->mapFrom(viewport(), e->pos()));
        if (!child || child == m_container) {
            for (ThumbnailTileWidget* t : m_tiles) t->setSelected(false);
            m_lastClicked = nullptr;
            m_currentTile = nullptr;
            emit selectionChanged({});
        }
    }
    QScrollArea::mousePressEvent(e);
}
