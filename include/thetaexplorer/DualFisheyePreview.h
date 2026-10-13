#pragma once
#include <QImage>
#include <QString>

namespace DualFisheyePreview {

enum class Projection { Unknown, Equirectangular, DualFisheye, DualFisheyeUpright, Unsupported };

struct Result {
    QImage original;
    QImage stitched;
    Projection projection = Projection::Unknown;
    QString error;
};

Result read(const QString& filePath);
QImage reproject(const QImage& original, bool upright = false);

}
