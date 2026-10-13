#pragma once
#include <QObject>
#include <QHash>
#include <QString>
#include <QTimer>
#include <QPixmap>
#include <QSet>
#include <memory>
#include "thetaexplorer/CameraFileInfo.h"

class ThetaBridge;
class QTemporaryDir;
class CameraMediaCache;

// High-level Qt camera service - manages ThetaBridge and adds thumbnail throttling.
class ThetaCameraService : public QObject
{
    Q_OBJECT
public:
    explicit ThetaCameraService(QObject* parent = nullptr);
    ~ThetaCameraService();

    void start();
    void stop();
    void refresh();

    const QList<CameraFileInfo>& fileList() const { return m_files; }
    QString cameraIdentifier() const { return m_cameraIdentifier; }
    bool isCameraConnected() const { return m_connected; }
    bool isDownloadActive() const { return m_downloadActive || !m_downloadQueue.isEmpty() || m_recoveringAfterDownloadError || m_imageInfoActive || m_catalogEnrichmentPending; }
    bool isDeleteActive() const { return m_deleteActive; }

    void requestThumbnail(const CameraFileInfo& file);
    void requestPreview(const CameraFileInfo& file);
    void cancelPendingPreview() { m_pendingPreview = CameraFileInfo{}; }
    void downloadFiles(const QList<CameraFileInfo>& files, const QString& destinationPath);
    void deleteFiles(const QList<CameraFileInfo>& files);

signals:
    void cameraConnected(const QString& name);
    void cameraDisconnected();
    void batteryLevelChanged(int percent, bool available);
    void fileListReady(const QList<CameraFileInfo>& files);
    void thumbnailReady(const QString& devicePath, const QPixmap& thumbnail);
    void previewFileReady(const QString& devicePath, const QString& savedPath);
    void previewError(const QString& devicePath, const QString& message);
    void previewActivityChanged(bool active);
    void downloadProgress(const QString& fileName, int percent);
    void downloadFileCompleted(const QString& fileName, const QString& savedPath);
    void userDownloadVerified(const QString& cameraIdentifier, const CameraFileInfo& file,
                              const QString& savedPath);
    void downloadError(const QString& fileName, const QString& errorMessage);
    void deleteCompleted(const QStringList& deletedPaths);
    void errorOccurred(const QString& message);

private slots:
    void onFileListUpdated(const QList<CameraFileInfo>& files);
    void drainThumbnailQueue();
    void onBridgeDownloadProgress(const QString& fileName, int percent);
    void onBridgeDownloadFileCompleted(const QString& fileName, const QString& savedPath);
    void onBridgeDownloadError(const QString& fileName, const QString& errorMessage);
    void onBridgeDeleteCompleted(const QStringList& deletedPaths);
    void onBridgeDeleteError(const QString& errorMessage);
    void resumeAfterRecovery();
    void kickNextDownload();
    void onDeleteTimedOut();

private:
    void publishCatalog();
    void finishImageInfo(const CameraFileInfo& file, const QPixmap& thumbnail,
                         const QString& error, quint64 requestId);
    bool currentFile(const CameraFileInfo& file) const;
    bool cacheEligible(const CameraFileInfo& file) const;
    void warnCacheFailure();
    void acceptThumbnail(const CameraFileInfo& file, const QPixmap& thumbnail, quint64 requestId);
    struct PendingThumbnail {
        CameraFileInfo file;
        QString cameraIdentifier;
    };
    struct PendingDownload {
        CameraFileInfo file;
        QString destinationPath;
        int attempt = 1;
        bool preview = false;
        QString cameraIdentifier;
        bool persistentPreview = false;
    };

    ThetaBridge*          m_bridge       = nullptr;
    QString              m_cameraIdentifier;
    std::unique_ptr<CameraMediaCache> m_mediaCache;
    QHash<quint64, PendingThumbnail> m_pendingThumbnails;
    quint64 m_thumbnailRequestId = 0;
    QString m_activeImageInfoCameraIdentifier;
    QString m_pendingDeleteCameraIdentifier;
    bool m_catalogComplete = false;
    bool m_cacheWarningShown = false;
    QList<CameraFileInfo> m_files;
    QList<CameraFileInfo> m_thumbnailQueue;
    QList<CameraFileInfo> m_imageInfoQueue;
    QHash<QString, QPixmap> m_rawThumbnailCache;
    QSet<QString> m_rawFallbackAttempted;
    QTimer* m_imageInfoTimer = nullptr;
    CameraFileInfo m_activeImageInfo;
    quint64 m_imageInfoRequestId = 0;
    bool m_imageInfoActive = false;
    bool m_catalogEnrichmentPending = false;
    bool m_catalogMetadataFailed = false;
    bool m_headerRecovery = false;
    QList<PendingDownload> m_downloadQueue;
    PendingDownload        m_activeDownload;
    CameraFileInfo         m_pendingPreview;
    std::unique_ptr<QTemporaryDir> m_previewDirectory;
    QHash<QString, QString> m_previewCache;
    QStringList            m_previewCacheOrder;
    QTimer*               m_thumbnailTimer = nullptr;
    bool                  m_connected    = false;
    bool                  m_downloadActive = false;
    bool                  m_deleteActive = false;
    bool                  m_waitingForDeleteVerification = false;
    bool                  m_recoveringAfterDownloadError = false;
    int                   m_maxRecoveryRetries = 2;
    int                   m_interDownloadDelayMs = 180;
    int                   m_postErrorDelayMs = 900;
    int                   m_deleteTimeoutMs = 8000;
    QHash<QString, int>   m_failureCountByDevicePath;
    QList<CameraFileInfo> m_pendingDeleteFiles;
    QTimer*               m_deleteTimer = nullptr;
};
