#pragma once
#include <QWidget>
#include <QLabel>
#include <QStackedWidget>
#include <QFutureWatcher>
#include "thetaexplorer/DualFisheyePreview.h"
#include "thetaexplorer/MediaAssetGroup.h"

class PreviewPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PreviewPanel(QWidget* parent = nullptr);

    void showGroup(const MediaAssetGroup& group, const QPixmap& thumbnail);
    void showPreviewFile(const MediaAssetGroup& group, const QString& filePath);
    void showPreviewError(const QString& devicePath, const QString& message);
    void showMultipleSelection(int count);
    void clearPreview();

signals:
    void previewFailed(const QString& message);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void updateScaledImage();
    void invalidatePreview();
    void startPendingPreview();
    void applyPreview(const DualFisheyePreview::Result& result);

    struct CachedPreview {
        QString groupId;
        QString filePath;
        DualFisheyePreview::Result result;
    };

    QStackedWidget* m_stack       = nullptr;
    QLabel*         m_imageLabel  = nullptr;   // page 0
    QLabel*         m_stitchedLabel = nullptr;
    QLabel*         m_originalTitle = nullptr;
    QLabel*         m_bracketLabel = nullptr;
    QLabel*         m_approximateLabel = nullptr;
    QLabel*         m_videoLabel  = nullptr;   // page 1
    QLabel*         m_emptyLabel  = nullptr;   // page 2
    QLabel*         m_multiLabel  = nullptr;   // page 3

    QPixmap         m_originalPixmap;
    QPixmap         m_stitchedPixmap;
    bool            m_showingImage = false;
    bool            m_hasFullPreview = false;
    MediaAssetGroup m_currentGroup;
    quint64         m_generation = 0;
    QFutureWatcher<DualFisheyePreview::Result>* m_watcher = nullptr;
    bool            m_workerActive = false;
    QString         m_runningGroupId;
    QString         m_runningFilePath;
    quint64         m_runningGeneration = 0;
    QString         m_pendingFilePath;
    QList<CachedPreview> m_cache;
};
