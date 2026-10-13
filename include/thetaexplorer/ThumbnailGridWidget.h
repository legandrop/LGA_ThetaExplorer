#pragma once
#include <QScrollArea>
#include <QGridLayout>
#include <QList>
#include <QHash>
#include <QPixmap>
#include "thetaexplorer/MediaAssetGroup.h"

class ThumbnailTileWidget;

class ThumbnailGridWidget : public QScrollArea
{
    Q_OBJECT
public:
    explicit ThumbnailGridWidget(QWidget* parent = nullptr);

    void setGroups(const QList<MediaAssetGroup>& groups);
    void setThumbnail(const QString& devicePath, const QPixmap& thumbnail);
    void removeFiles(const QStringList& devicePaths);
    void clearAll();

    QList<MediaAssetGroup> selectedGroups() const;
    int snappedWidth(int requestedWidth, int maximumWidth) const;

signals:
    void selectionChanged(const QList<MediaAssetGroup>& selected);
    void thumbnailsNeeded(const QList<CameraFileInfo>& files);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void onTileClicked(ThumbnailTileWidget* tile, Qt::KeyboardModifiers mods);

private:
    void selectTile(ThumbnailTileWidget* tile, Qt::KeyboardModifiers mods);
    void relayoutGrid();
    int  columnCount() const;

    QWidget*                     m_container  = nullptr;
    QGridLayout*                 m_layout     = nullptr;
    QList<ThumbnailTileWidget*>  m_tiles;
    ThumbnailTileWidget*         m_lastClicked = nullptr;
    ThumbnailTileWidget*         m_currentTile = nullptr;
    QHash<QString, QPixmap>      m_thumbnailCache;

    static constexpr int SPACING = 8;
    static constexpr int LEFT_MARGIN = 8;
    static constexpr int RIGHT_MARGIN = 4;
};
