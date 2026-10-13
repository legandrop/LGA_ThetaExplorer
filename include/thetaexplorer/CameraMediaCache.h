#pragma once

#include <QPixmap>
#include <QString>
#include <QJsonObject>
#include "thetaexplorer/CameraFileInfo.h"

// Cache regenerable: no contiene descargas del usuario ni handles nativos.
class CameraMediaCache
{
public:
    explicit CameraMediaCache(const QString& rootDirectory);
    QPixmap thumbnail(const QString& cameraId, const CameraFileInfo& file) const;
    bool storeThumbnail(const QString& cameraId, const CameraFileInfo& file, const QPixmap& image);
    QString previewFile(const QString& cameraId, const CameraFileInfo& file) const;
    QString preparePreviewDirectory(const QString& cameraId, const CameraFileInfo& file);
    bool commitPreview(const QString& cameraId, const CameraFileInfo& file, const QString& receivedPath);
    void enrichMetadata(const QString& cameraId, CameraFileInfo& file) const;
    bool storeMetadata(const QString& cameraId, const CameraFileInfo& file);
    bool reconcile(const QString& cameraId, const QList<CameraFileInfo>& completeFiles);
    bool removeFiles(const QString& cameraId, const QList<CameraFileInfo>& files);
    QString errorString() const { return m_error; }

private:
    static QString fileKey(const CameraFileInfo& file);
    QString cameraDirectory(const QString& cameraId) const;
    QString entryDirectory(const QString& cameraId, const CameraFileInfo& file) const;
    bool ensureDirectory(const QString& directory);
    bool writeJson(const QString& path, const QJsonObject& value);
    bool removeEntry(const QString& directory);
    QString m_root;
    QString m_error;
};
