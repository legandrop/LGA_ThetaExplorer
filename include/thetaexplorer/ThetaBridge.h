#pragma once
#include <QObject>
#include <QPixmap>
#include <QStringList>
#include "thetaexplorer/CameraFileInfo.h"

// Forward-declare private implementation (lives in .mm, never exposed to C++)
class ThetaBridgePrivate;

// ThetaBridge: pure Qt QObject that wraps ImageCaptureCore (Objective-C).
// This header contains NO Objective-C types - safe to include from any .cpp file.
class ThetaBridge : public QObject
{
    Q_OBJECT
public:
    explicit ThetaBridge(QObject* parent = nullptr);
    ~ThetaBridge();

    void startBrowsing();
    void stopBrowsing();
    void refreshCatalog();
    void recoverDownloadSession();

    void requestThumbnail(const CameraFileInfo& file);
    void requestImageInfo(const CameraFileInfo& file, quint64 requestId);
    void requestDownloadFile(const CameraFileInfo& file,
                             const QString& destinationPath);
    void requestDownloadFiles(const QList<CameraFileInfo>& files,
                              const QString& destinationPath);
    void requestDeleteFiles(const QList<CameraFileInfo>& files);

signals:
    // Camera lifecycle
    void cameraConnected(const QString& cameraName);
    void cameraDisconnected();
    void cameraError(const QString& errorMessage);

    // File list - emitted once camera catalog is ready
    void fileListUpdated(const QList<CameraFileInfo>& files);

    // Thumbnails - emitted per-file as they arrive asynchronously
    void thumbnailReady(const QString& devicePath, const QPixmap& thumbnail);
    void thumbnailFailed(const QString& devicePath);
    void imageInfoReady(const CameraFileInfo& file, const QPixmap& thumbnail, quint64 requestId);
    void imageInfoFailed(const QString& devicePath, const QString& message, quint64 requestId);

    // Downloads
    void downloadProgress(const QString& fileName, int percentComplete);
    void downloadFileCompleted(const QString& fileName, const QString& savedPath);
    void downloadError(const QString& fileName, const QString& errorMessage);
    void batteryLevelChanged(int percent, bool available);

    // Delete
    void deleteCompleted(const QStringList& deletedPaths);
    void deleteError(const QString& errorMessage);

// These are intentionally public so ThetaBridgePrivate (in .mm) can call them
// via QMetaObject::invokeMethod. Private members can't be QMetaObject targets.
public:
    // Pimpl pointer - Obj-C objects live here
    ThetaBridgePrivate* d = nullptr;
};
