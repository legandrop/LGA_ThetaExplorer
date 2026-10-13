#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QList>
#include "thetaexplorer/CameraFileInfo.h"

enum class MediaAssetKind {
    SinglePhoto,
    SingleRaw,
    Video,
    HdrJpegSet,
    HdrRawSet,
    HdrCombinedSet,
};

struct MediaAssetGroup {
    QString id;
    MediaAssetKind kind = MediaAssetKind::SinglePhoto;
    QList<CameraFileInfo> files;
    CameraFileInfo representative;
    int representativeIndex = 0;
    bool isHdrSet = false;
    bool isRaw = false;
    bool isVideo = false;
    bool allFilesDownloaded = false;
    bool hasLocalContent = false;
    bool hasPartialDownloadHistory = false;
    int downloadedFileCount = 0;
    QDateTime lastDownloadedAt;
    QDateTime captureTime;
    QString displayTitle;
    QString subtitle;
    QString representativeDevicePath() const { return representative.devicePath; }
    int imageCount() const { return files.size(); }
    bool isCombinedHdr() const { return kind == MediaAssetKind::HdrCombinedSet; }
    int bracketCount() const { return isCombinedHdr() ? files.size() / 2 : files.size(); }
};

Q_DECLARE_METATYPE(MediaAssetGroup)
Q_DECLARE_METATYPE(QList<MediaAssetGroup>)
