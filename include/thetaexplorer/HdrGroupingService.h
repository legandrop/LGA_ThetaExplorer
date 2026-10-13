#pragma once

#include <QList>
#include "thetaexplorer/CameraFileInfo.h"
#include "thetaexplorer/MediaAssetGroup.h"

class HdrGroupingService
{
public:
    static QList<MediaAssetGroup> buildGroups(const QList<CameraFileInfo>& files);
    static QList<MediaAssetGroup> downloadGroups(const MediaAssetGroup& group);
};
