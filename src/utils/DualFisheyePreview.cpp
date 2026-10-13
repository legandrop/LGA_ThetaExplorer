#include "thetaexplorer/DualFisheyePreview.h"
#include <QFile>
#include <QColorSpace>
#include <QDebug>
#include <QImageReader>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxPreviewWidth = 1600;

DualFisheyePreview::Projection exifProjection(const QByteArray& tiff)
{
    using Projection = DualFisheyePreview::Projection;
    if (tiff.size() < 8 || (tiff.left(2) != "II" && tiff.left(2) != "MM")) {
        return Projection::Unknown;
    }
    const bool littleEndian = tiff.left(2) == "II";
    auto valid = [&tiff](quint32 offset, quint32 length) {
        return offset <= quint32(tiff.size()) && length <= quint32(tiff.size()) - offset;
    };
    auto u16 = [&](quint32 offset) -> quint16 {
        if (!valid(offset, 2)) return 0;
        const auto* bytes = reinterpret_cast<const uchar*>(tiff.constData() + offset);
        return littleEndian ? qFromLittleEndian<quint16>(bytes) : qFromBigEndian<quint16>(bytes);
    };
    auto u32 = [&](quint32 offset) -> quint32 {
        if (!valid(offset, 4)) return 0;
        const auto* bytes = reinterpret_cast<const uchar*>(tiff.constData() + offset);
        return littleEndian ? qFromLittleEndian<quint32>(bytes) : qFromBigEndian<quint32>(bytes);
    };
    auto findEntry = [&](quint32 ifd, quint16 tag) -> quint32 {
        if (!valid(ifd, 2)) return 0;
        const quint32 count = u16(ifd);
        if (!valid(ifd + 2, count * 12)) return 0;
        for (quint32 i = 0; i < count; ++i) {
            const quint32 entry = ifd + 2 + i * 12;
            if (u16(entry) == tag) return entry;
        }
        return 0;
    };
    if (u16(2) != 42) return Projection::Unknown;
    const quint32 exifEntry = findEntry(u32(4), 0x8769);
    if (!exifEntry || u16(exifEntry + 2) != 4 || u32(exifEntry + 4) != 1) {
        return Projection::Unknown;
    }
    const quint32 makerEntry = findEntry(u32(exifEntry + 8), 0x927c);
    if (!makerEntry || u16(makerEntry + 2) != 7) return Projection::Unknown;
    const quint32 maker = u32(makerEntry + 8);
    const quint32 makerLength = u32(makerEntry + 4);
    if (makerLength < 10 || !valid(maker, makerLength)
        || tiff.mid(maker, 8) != QByteArray("Ricoh\0\0\0", 8)) {
        return Projection::Unknown;
    }
    // Los offsets RICOH son relativos al encabezado TIFF, también dentro de MakerNote.
    const quint32 thetaEntry = findEntry(maker + 8, 0x4001);
    if (!thetaEntry || u16(thetaEntry + 2) != 4 || u32(thetaEntry + 4) != 1) {
        return Projection::Unknown;
    }
    const quint32 imageType = findEntry(u32(thetaEntry + 8), 0x0001);
    if (!imageType || u16(imageType + 2) != 3 || u32(imageType + 4) != 1) {
        return Projection::Unknown;
    }
    switch (u16(imageType + 8)) {
        case 1: return Projection::Equirectangular;
        case 2: return Projection::DualFisheye;
        case 3: return Projection::DualFisheyeUpright;
        default: return Projection::Unsupported;
    }
}

DualFisheyePreview::Projection jpegProjection(const QString& filePath)
{
    using Projection = DualFisheyePreview::Projection;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly) || file.read(2) != QByteArray("\xff\xd8", 2)) {
        return Projection::Unknown;
    }
    // Sólo se leen segmentos previos a la imagen; cada APP1 tiene como máximo 64 KiB.
    while (file.pos() < 1024 * 1024) {
        char prefix = 0;
        char marker = 0;
        if (!file.getChar(&prefix) || uchar(prefix) != 0xff) break;
        do {
            if (file.pos() >= 1024 * 1024 || !file.getChar(&marker)) return Projection::Unknown;
        } while (uchar(marker) == 0xff);
        if (uchar(marker) == 0xda || uchar(marker) == 0xd9) break;
        if (uchar(marker) == 0x01 || (uchar(marker) >= 0xd0 && uchar(marker) <= 0xd7)) continue;
        const QByteArray lengthBytes = file.read(2);
        if (lengthBytes.size() != 2) break;
        const int length = qFromBigEndian<quint16>(lengthBytes.constData()) - 2;
        if (length < 0) break;
        const QByteArray segment = file.read(length);
        if (segment.size() != length) break;
        if (uchar(marker) == 0xe1 && segment.startsWith(QByteArray("Exif\0\0", 6))) {
            const Projection projection = exifProjection(segment.mid(6));
            if (projection != Projection::Unknown) return projection;
        }
    }
    return Projection::Unknown;
}

QRgb bilinear(const QImage& image, double x, double y)
{
    x = std::clamp(x, 0.0, double(image.width() - 1));
    y = std::clamp(y, 0.0, double(image.height() - 1));
    const int x0 = int(x);
    const int y0 = int(y);
    const int x1 = std::min(x0 + 1, image.width() - 1);
    const int y1 = std::min(y0 + 1, image.height() - 1);
    const auto* row0 = reinterpret_cast<const QRgb*>(image.constScanLine(y0));
    const auto* row1 = reinterpret_cast<const QRgb*>(image.constScanLine(y1));
    const double fx = x - x0;
    const double fy = y - y0;
    auto channel = [&](int shift) {
        const double top = ((row0[x0] >> shift) & 255) * (1.0 - fx)
                           + ((row0[x1] >> shift) & 255) * fx;
        const double bottom = ((row1[x0] >> shift) & 255) * (1.0 - fx)
                              + ((row1[x1] >> shift) & 255) * fx;
        return int(top * (1.0 - fy) + bottom * fy + 0.5);
    };
    return qRgb(channel(16), channel(8), channel(0));
}
}

QImage DualFisheyePreview::reproject(const QImage& original, bool upright)
{
    if (original.isNull() || original.width() < 4 || original.height() < 2
        || std::abs(original.width() - 2 * original.height()) > 2) {
        return {};
    }
    const QImage source = original.convertToFormat(QImage::Format_RGB32);
    const int width = std::min(kMaxPreviewWidth, source.width()) & ~1;
    const int height = width / 2;
    QImage output(width, height, QImage::Format_RGB32);
    if (output.isNull()) return {};
    output.setColorSpace(source.colorSpace());
    const double lensWidth = source.width() / 2.0;
    const double center = (lensWidth - 1.0) / 2.0;
    const double centerY = (source.height() - 1.0) / 2.0;
    // shortcut: óptica equidistante ideal de 190°, usar calibración por cámara para un stitch final.
    const double focal = lensWidth / (190.0 * kPi / 180.0);
    std::vector<double> sinLongitude(width), cosLongitude(width);
    for (int x = 0; x < width; ++x) {
        const double longitude = (double(x) + 0.5) * 2.0 * kPi / width - kPi;
        sinLongitude[x] = std::sin(longitude);
        cosLongitude[x] = std::cos(longitude);
    }
    for (int y = 0; y < height; ++y) {
        const double latitude = (double(y) + 0.5) * kPi / height - kPi / 2.0;
        const double horizontal = std::cos(latitude);
        const double vertical = std::sin(latitude);
        auto* row = reinterpret_cast<QRgb*>(output.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const double dx = horizontal * sinLongitude[x];
            const double dz = horizontal * cosLongitude[x];
            const bool right = dz >= 0;
            const double radial = std::hypot(dx, vertical);
            const double scale = radial > 1e-12
                ? focal * std::acos(std::clamp(std::abs(dz), 0.0, 1.0)) / radial : 0.0;
            const double u = (right ? dx : -dx) * scale;
            const double v = vertical * scale;
            // Tipo 2: izquierda gira CW90 y derecha CCW90; muestrear el original sin copiarlo.
            const double sampleX = upright ? u : (right ? -v : v);
            const double sampleY = upright ? v : (right ? u : -u);
            row[x] = bilinear(source, (right ? lensWidth : 0.0) + center + sampleX,
                              centerY + sampleY);
        }
    }
    return output;
}

DualFisheyePreview::Result DualFisheyePreview::read(const QString& filePath)
{
    Result result;
    result.projection = jpegProjection(filePath);
    QImageReader reader(filePath);
    reader.setAutoTransform(false);
    const QSize nativeSize = reader.size();
    if (!nativeSize.isValid()) {
        qWarning() << "No se pudo leer la imagen de preview:" << reader.errorString();
        result.error = QStringLiteral("Could not read image preview.");
        return result;
    }
    if (nativeSize.width() > kMaxPreviewWidth || nativeSize.height() > kMaxPreviewWidth) {
        reader.setScaledSize(nativeSize.scaled(kMaxPreviewWidth, kMaxPreviewWidth, Qt::KeepAspectRatio));
    }
    result.original = reader.read();
    if (result.original.isNull()) {
        qWarning() << "No se pudo decodificar la imagen de preview:" << reader.errorString();
        result.error = QStringLiteral("Could not load image preview.");
        return result;
    }
    if (result.original.width() > kMaxPreviewWidth || result.original.height() > kMaxPreviewWidth) {
        result.original = result.original.scaled(kMaxPreviewWidth, kMaxPreviewWidth,
                                                 Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (result.projection == Projection::Equirectangular) {
        result.stitched = result.original;
    } else if (result.projection == Projection::DualFisheye
               || result.projection == Projection::DualFisheyeUpright) {
        result.stitched = reproject(result.original, result.projection == Projection::DualFisheyeUpright);
        if (result.stitched.isNull()) {
            qWarning() << "Tamaño dual fisheye no soportado para la preview:" << result.original.size();
            result.error = QStringLiteral("This dual fisheye image has an unsupported size.");
        }
    }
    return result;
}
