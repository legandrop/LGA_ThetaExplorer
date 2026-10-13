#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include "thetaexplorer/CameraFileInfo.h"

class DownloadHistory
{
public:
    explicit DownloadHistory(const QString& path);
    bool reload();
    bool record(const QString& cameraId, const CameraFileInfo& file,
                const QDateTime& downloadedAt = QDateTime::currentDateTimeUtc());
    QDateTime lastDownloadedAt(const QString& cameraId, const CameraFileInfo& file) const;
    QString errorString() const { return m_error; }

private:
    static QString fileKey(const QString& cameraId, const CameraFileInfo& file);
    bool read(QJsonObject& entries, QString& error) const;

    QString m_path;
    QJsonObject m_entries;
    QString m_error;
};
