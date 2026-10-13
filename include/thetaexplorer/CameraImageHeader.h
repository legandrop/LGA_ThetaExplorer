#pragma once
#include <QByteArray>
#include <QImage>
#include "thetaexplorer/CameraFileInfo.h"

namespace CameraImageHeader {
struct Result {
    CameraFileInfo file;
    QImage thumbnail;
    QString error;
    bool headerValid = false;
};
Result parse(const CameraFileInfo& file, const QByteArray& header);
}
