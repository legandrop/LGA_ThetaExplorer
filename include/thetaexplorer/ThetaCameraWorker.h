#pragma once
#include <QJsonObject>
#include "thetaexplorer/CameraFileInfo.h"

namespace ThetaCameraWorker {
constexpr int MaxLineBytes = 8 * 1024 * 1024;
constexpr int MaxHeaderBytes = 1024 * 1024;
constexpr int MaxThumbnailBytes = 4 * 1024 * 1024;
constexpr int MaxThumbnailDimension = 1000;

QJsonObject fileToJson(const CameraFileInfo& file);
bool fileFromJson(const QJsonObject& object, CameraFileInfo& file);
int runThetaCameraWorker(int argc, char** argv);
}
