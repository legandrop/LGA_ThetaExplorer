#include "thetaexplorer/CameraImageHeader.h"
#include <QBuffer>
#include <QImageReader>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kHeaderLimit = 1024 * 1024;

QImage decodeThumbnail(const QByteArray& bytes)
{
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "JPEG");
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > 50000 || size.height() > 50000) return {};
    reader.setScaledSize(size.scaled(1000, 1000, Qt::KeepAspectRatio));
    return reader.read();
}

class TiffHeader
{
public:
    explicit TiffHeader(const QByteArray& bytes) : data(bytes), little(bytes.startsWith("II")) {}

    bool contains(quint64 offset, quint64 length) const
    {
        return offset <= quint64(data.size()) && length <= quint64(data.size()) - offset;
    }
    quint16 word(quint64 offset) const
    {
        if (!contains(offset, 2)) return 0;
        const auto* p = reinterpret_cast<const uchar*>(data.constData() + offset);
        return little ? qFromLittleEndian<quint16>(p) : qFromBigEndian<quint16>(p);
    }
    quint32 dword(quint64 offset) const
    {
        if (!contains(offset, 4)) return 0;
        const auto* p = reinterpret_cast<const uchar*>(data.constData() + offset);
        return little ? qFromLittleEndian<quint32>(p) : qFromBigEndian<quint32>(p);
    }
    quint64 values(quint64 entry, quint32& count, quint16& type) const
    {
        type = word(entry + 2);
        count = dword(entry + 4);
        const quint32 unit = type == 3 ? 2 : (type == 4 ? 4 : (type == 5 || type == 10 ? 8 : 0));
        if (!unit || !count) return quint64(data.size()) + 1;
        const quint64 length = quint64(count) * unit;
        const quint64 offset = length <= 4 ? entry + 8 : dword(entry + 8);
        return contains(offset, length) ? offset : quint64(data.size()) + 1;
    }
    quint32 integer(quint64 entry) const
    {
        quint32 count = 0;
        quint16 type = 0;
        const quint64 offset = values(entry, count, type);
        if (!contains(offset, type == 3 ? 2 : 4)) return 0;
        return type == 3 ? word(offset) : (type == 4 ? dword(offset) : 0);
    }
    double rational(quint64 entry) const
    {
        quint32 count = 0;
        quint16 type = 0;
        const quint64 offset = values(entry, count, type);
        if (count != 1 || type != 5 || !contains(offset, 8)) return 0;
        const quint32 denominator = dword(offset + 4);
        return denominator ? double(dword(offset)) / denominator : 0;
    }

    bool read(CameraImageHeader::Result& result) const
    {
        if (data.size() < 8 || (data.left(2) != "II" && data.left(2) != "MM") || word(2) != 42) return false;
        QList<quint32> pending{dword(4)};
        QSet<quint32> visited;
        double seconds = 0, aperture = 0, iso = 0;
        quint64 largestArea = 0;
        while (!pending.isEmpty() && visited.size() < 64) {
            const quint32 ifd = pending.takeFirst();
            if (!ifd || visited.contains(ifd)) continue;
            visited.insert(ifd);
            if (!contains(ifd, 2)) continue;
            const quint32 entries = word(ifd);
            if (entries > 4096 || !contains(quint64(ifd) + 2, quint64(entries) * 12 + 4)) return false;
            quint32 width = 0, height = 0, jpegOffset = 0, jpegLength = 0;
            quint32 stripOffset = 0, stripLength = 0, compression = 0;
            for (quint32 i = 0; i < entries; ++i) {
                const quint64 entry = quint64(ifd) + 2 + quint64(i) * 12;
                const quint16 tag = word(entry);
                if (tag == 0x8769 || tag == 0x014a) {
                    quint32 count = 0;
                    quint16 type = 0;
                    const quint64 offset = values(entry, count, type);
                    if (type == 4 && count <= 64 && contains(offset, quint64(count) * 4)) {
                        for (quint32 j = 0; j < count; ++j) pending.append(dword(offset + quint64(j) * 4));
                    }
                    continue;
                }
                switch (tag) {
                    case 0x0100: case 0xa002: width = integer(entry); break;
                    case 0x0101: case 0xa003: height = integer(entry); break;
                    case 0x0103: compression = integer(entry); break;
                    case 0x0201: jpegOffset = integer(entry); break;
                    case 0x0202: jpegLength = integer(entry); break;
                    case 0x0111: if (dword(entry + 4) == 1) stripOffset = integer(entry); break;
                    case 0x0117: if (dword(entry + 4) == 1) stripLength = integer(entry); break;
                    case 0xa402:
                        if (result.file.exposureMode < 0 && word(entry + 2) == 3 && dword(entry + 4) == 1) {
                            const quint32 mode = integer(entry);
                            if (mode <= 2) result.file.exposureMode = int(mode);
                        }
                        break;
                    case 0x829a: if (seconds <= 0) seconds = rational(entry); break;
                    case 0x829d: if (aperture <= 0) aperture = rational(entry); break;
                    case 0x8827: if (iso <= 0) iso = integer(entry); break;
                    case 0x8833: if (iso <= 0 || iso == 65535) iso = integer(entry); break;
                    default: break;
                }
            }
            if (width && height && width <= 100000 && height <= 100000
                && quint64(width) * height > largestArea) {
                largestArea = quint64(width) * height;
                result.file.width = int(width);
                result.file.height = int(height);
            }
            if (!jpegOffset && compression == 7) { jpegOffset = stripOffset; jpegLength = stripLength; }
            if (jpegOffset && jpegLength && contains(jpegOffset, jpegLength)) {
                const QImage image = decodeThumbnail(data.mid(jpegOffset, jpegLength));
                if (!image.isNull() && (result.thumbnail.isNull()
                    || image.width() * image.height() > result.thumbnail.width() * result.thumbnail.height())) {
                    result.thumbnail = image;
                }
            }
            pending.append(dword(quint64(ifd) + 2 + quint64(entries) * 12));
        }
        if (std::isfinite(seconds) && seconds > 0) result.file.exposureTime = seconds;
        if (std::isfinite(seconds) && std::isfinite(iso) && std::isfinite(aperture)
            && seconds > 0 && iso > 0 && aperture > 0) {
            const double exposure = std::log2(seconds) + std::log2(iso) - 2 * std::log2(aperture);
            if (std::isfinite(exposure)) result.file.exposureValue = exposure;
        }
        return true;
    }
private:
    const QByteArray& data;
    bool little;
};
}

CameraImageHeader::Result CameraImageHeader::parse(const CameraFileInfo& file, const QByteArray& header)
{
    Result result;
    result.file = file;
    if (header.isEmpty() || header.size() > kHeaderLimit) {
        result.error = QStringLiteral("The image header has an unsupported size.");
        return result;
    }
    bool valid = false;
    if (header.startsWith(QByteArray("\xff\xd8", 2))) {
        quint64 offset = 2;
        valid = true;
        while (offset + 4 <= quint64(header.size())) {
            if (uchar(header[int(offset++)]) != 0xff) { valid = false; break; }
            while (offset < quint64(header.size()) && uchar(header[int(offset)]) == 0xff) ++offset;
            if (offset >= quint64(header.size())) break;
            const uchar marker = uchar(header[int(offset++)]);
            if (marker == 0xda || marker == 0xd9) break;
            if (marker == 1 || (marker >= 0xd0 && marker <= 0xd7)) continue;
            if (offset + 2 > quint64(header.size())) break;
            const quint16 length = qFromBigEndian<quint16>(header.constData() + offset);
            if (length < 2) { valid = false; break; }
            if (offset + length > quint64(header.size())) break;
            const QByteArray segment = header.mid(offset + 2, length - 2);
            if (marker == 0xe1 && segment.startsWith(QByteArray("Exif\0\0", 6))) {
                const QByteArray tiff = segment.mid(6);
                if (!TiffHeader(tiff).read(result)) { valid = false; break; }
            } else if ((marker >= 0xc0 && marker <= 0xc3) && segment.size() >= 5) {
                if (result.file.height <= 0) result.file.height = qFromBigEndian<quint16>(segment.constData() + 1);
                if (result.file.width <= 0) result.file.width = qFromBigEndian<quint16>(segment.constData() + 3);
            }
            offset += length;
        }
    } else {
        valid = TiffHeader(header).read(result);
    }
    result.headerValid = valid;
    if (!valid) result.error = QStringLiteral("The image header is not readable.");
    else if (file.isRaw && result.thumbnail.isNull()) result.error = QStringLiteral("The RAW embedded preview could not be read.");
    return result;
}
