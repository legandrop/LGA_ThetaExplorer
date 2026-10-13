#include "thetaexplorer/HdrGroupingService.h"
#include "thetaexplorer/Logger.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace {

struct ParsedName {
    QString extension;
    QString prefix;
    int number = -1;
    bool hasNumber = false;
};

ParsedName parseName(const QString& fileName)
{
    ParsedName parsed;
    const QFileInfo info(fileName);
    parsed.extension = info.suffix().toLower();

    static const QRegularExpression regex("^(.*?)(\\d+)$");
    const QString stem = info.completeBaseName();
    const QRegularExpressionMatch match = regex.match(stem);
    if (match.hasMatch()) {
        parsed.prefix = match.captured(1).toUpper();
        parsed.number = match.captured(2).toInt();
        parsed.hasNumber = true;
    } else {
        parsed.prefix = stem.toUpper();
    }
    return parsed;
}

bool isBrowsable(const CameraFileInfo& file)
{
    if (file.isVideo || file.isRaw) {
        return true;
    }

    const QString ext = QFileInfo(file.name).suffix().toLower();
    return ext == "jpg" || ext == "jpeg";
}

bool sameFamily(const CameraFileInfo& a, const CameraFileInfo& b)
{
    if (a.isVideo || b.isVideo) {
        return a.isVideo == b.isVideo;
    }
    if (a.isRaw != b.isRaw) {
        return false;
    }
    const QString extA = QFileInfo(a.name).suffix().toLower();
    const QString extB = QFileInfo(b.name).suffix().toLower();
    return extA == extB;
}

void inheritPairedJpegExposure(QList<CameraFileInfo>& files)
{
    QMultiHash<QString, int> jpegByBase;
    for (int i = 0; i < files.size(); ++i) {
        const CameraFileInfo& file = files[i];
        const QString extension = QFileInfo(file.name).suffix().toLower();
        if (!file.isRaw && !file.isVideo && (extension == "jpg" || extension == "jpeg")) {
            jpegByBase.insert(QFileInfo(file.name).completeBaseName().toUpper(), i);
        }
    }
    QList<int> candidateCounts(files.size(), 0);
    QList<int> pairedIndexes(files.size(), -1);
    for (int i = 0; i < files.size(); ++i) {
        const CameraFileInfo& raw = files[i];
        if (!raw.isRaw || raw.isVideo || !raw.creationDate.isValid() || raw.devicePath.isEmpty()
            || QFileInfo(raw.name).suffix().compare("dng", Qt::CaseInsensitive) != 0) continue;
        const QString base = QFileInfo(raw.name).completeBaseName().toUpper();
        if (base.isEmpty()) continue;
        const QString directory = QDir::cleanPath(QFileInfo(raw.devicePath).path());
        for (int j : jpegByBase.values(base)) {
            const CameraFileInfo& jpeg = files[j];
            if (!jpeg.creationDate.isValid() || jpeg.devicePath.isEmpty()
                || QDir::cleanPath(QFileInfo(jpeg.devicePath).path()) != directory) continue;
            const qint64 gapMs = raw.creationDate.msecsTo(jpeg.creationDate);
            if (gapMs < -5 * 60 * 1000 || gapMs > 5 * 60 * 1000) continue;
            ++candidateCounts[i];
            ++candidateCounts[j];
            pairedIndexes[i] = j;
        }
    }
    // Un JPG identifica la misma toma DNG; no reemplazar metadatos explícitos del RAW.
    for (int i = 0; i < files.size(); ++i) {
        const int j = pairedIndexes[i];
        if (j < 0 || candidateCounts[i] != 1 || candidateCounts[j] != 1) continue;
        CameraFileInfo& raw = files[i];
        const CameraFileInfo& jpeg = files[j];
        if (raw.exposureMode < 0) raw.exposureMode = jpeg.exposureMode;
        if (!std::isfinite(raw.exposureValue)) raw.exposureValue = jpeg.exposureValue;
        if (!std::isfinite(raw.exposureTime)) raw.exposureTime = jpeg.exposureTime;
    }
}

bool areAdjacentHdrCandidates(const QList<CameraFileInfo>& run, const CameraFileInfo& current)
{
    const CameraFileInfo& previous = run.last();
    if (!sameFamily(previous, current)) {
        return false;
    }
    if (previous.isVideo || current.isVideo) {
        return false;
    }
    if (previous.devicePath.isEmpty() || current.devicePath.isEmpty()
        || QDir::cleanPath(QFileInfo(previous.devicePath).path())
            != QDir::cleanPath(QFileInfo(current.devicePath).path())) return false;

    const ParsedName prevName = parseName(previous.name);
    const ParsedName currName = parseName(current.name);

    if (prevName.prefix != currName.prefix) {
        return false;
    }

    constexpr double minExposureStep = 0.25;
    if (previous.exposureMode == 2 || current.exposureMode == 2) {
        if (previous.exposureMode != 2 || current.exposureMode != 2
            || !std::isfinite(previous.exposureValue) || !std::isfinite(current.exposureValue)
            || !prevName.hasNumber || !currName.hasNumber || currName.number - prevName.number != 1
            || !previous.creationDate.isValid() || !current.creationDate.isValid()) return false;
        const qint64 gapMs = previous.creationDate.msecsTo(current.creationDate);
        if (gapMs < 0 || gapMs > 5 * 60 * 1000) return false;
        const double step = current.exposureValue - previous.exposureValue;
        if (!std::isfinite(step) || std::abs(step) < minExposureStep) return false;
        return true;
    }

    // Una ráfaga automática o sin cambio de exposición no es un bracket.
    if (previous.exposureMode == 0 || current.exposureMode == 0) return false;
    if (std::isfinite(previous.exposureValue) && std::isfinite(current.exposureValue)
        && std::abs(current.exposureValue - previous.exposureValue) < minExposureStep) return false;

    if (!previous.creationDate.isValid() || !current.creationDate.isValid()) return false;
    {
        const qint64 gapMs = previous.creationDate.msecsTo(current.creationDate);
        if (gapMs < 0 || gapMs > 2500) {
            return false;
        }
    }

    if (prevName.hasNumber && currName.hasNumber) {
        const int delta = currName.number - prevName.number;
        return delta >= 1 && delta <= 2;
    }

    return true;
}

bool startsNewNativeCapture(const QList<CameraFileInfo>& run, int start, int index)
{
    if (run.first().exposureMode != 2) return false;

    // El orden puede ser arbitrario: confirmar el reinicio con las tres primeras exposiciones.
    if (index - start >= 3 && index + 2 < run.size()) {
        bool repeatedStart = true;
        for (int j = 0; j < 3; ++j) {
            if (std::abs(run[index + j].exposureValue - run[start + j].exposureValue) > 0.05) {
                repeatedStart = false;
                break;
            }
        }
        if (repeatedStart) return true;
    }

    const double seconds = run[index].exposureTime;
    const double previousSeconds = run[index - 1].exposureTime;
    if (!std::isfinite(seconds) || seconds <= 0 || !std::isfinite(previousSeconds) || previousSeconds <= 0) return false;
    QList<double> idleIntervals;
    for (int j = qMax(start + 1, index - 8); j < index; ++j) {
        const double exposure = run[j].exposureTime;
        const double previousExposure = run[j - 1].exposureTime;
        if (!std::isfinite(exposure) || exposure <= 0 || !std::isfinite(previousExposure) || previousExposure <= 0) return false;
        idleIntervals.append(qMax(0.0, run[j - 1].creationDate.msecsTo(run[j].creationDate) / 1000.0 - qMax(exposure, previousExposure)));
    }
    std::sort(idleIntervals.begin(), idleIntervals.end());
    const double cadence = idleIntervals.isEmpty() ? 0.0 : idleIntervals[idleIntervals.size() / 2];
    const double idle = run[index - 1].creationDate.msecsTo(run[index].creationDate) / 1000.0 - qMax(seconds, previousSeconds);
    // La fecha de archivo llega después de exponer; descontar el obturador evita cortar tomas largas.
    return idle > 30.0 && idle > 4.0 * cadence;
}

bool sameHdrCapture(const MediaAssetGroup& raw, const MediaAssetGroup& jpeg)
{
    if (raw.files.size() != jpeg.files.size() || raw.files.isEmpty() ||
        raw.id.isEmpty() || jpeg.id.isEmpty() || raw.id == jpeg.id) {
        return false;
    }

    QList<CameraFileInfo> rawFiles = raw.files;
    QList<CameraFileInfo> jpegFiles = jpeg.files;
    const auto baseName = [](const CameraFileInfo& file) {
        return QFileInfo(file.name).completeBaseName().toUpper();
    };
    const auto byBaseName = [&baseName](const CameraFileInfo& a, const CameraFileInfo& b) {
        return baseName(a) < baseName(b);
    };
    std::sort(rawFiles.begin(), rawFiles.end(), byBaseName);
    std::sort(jpegFiles.begin(), jpegFiles.end(), byBaseName);

    const QString directory = QDir::cleanPath(QFileInfo(rawFiles.first().devicePath).path());
    QString previousBase;
    for (int i = 0; i < rawFiles.size(); ++i) {
        const CameraFileInfo& a = rawFiles[i];
        const CameraFileInfo& b = jpegFiles[i];
        const QString stem = baseName(a);
        const QString jpegExtension = QFileInfo(b.name).suffix().toLower();
        if (stem.isEmpty() || stem == previousBase || stem != baseName(b) ||
            QFileInfo(a.name).suffix().compare("dng", Qt::CaseInsensitive) != 0 ||
            (jpegExtension != "jpg" && jpegExtension != "jpeg") ||
            a.devicePath.isEmpty() || b.devicePath.isEmpty() ||
            QDir::cleanPath(QFileInfo(a.devicePath).path()) != directory ||
            QDir::cleanPath(QFileInfo(b.devicePath).path()) != directory ||
            !a.creationDate.isValid() || !b.creationDate.isValid()) {
            return false;
        }
        // La fecha es de cada archivo: DNG y JPG se escriben en momentos distintos.
        constexpr qint64 maxVariantGapMs = 5 * 60 * 1000;
        const qint64 deltaMs = a.creationDate.msecsTo(b.creationDate);
        if (deltaMs < -maxVariantGapMs || deltaMs > maxVariantGapMs) {
            return false;
        }
        previousBase = stem;
    }
    return true;
}

QString kindBadge(const MediaAssetGroup& group)
{
    switch (group.kind) {
        case MediaAssetKind::HdrJpegSet:
            return QString("HDRI JPG · %1 brackets").arg(group.bracketCount());
        case MediaAssetKind::HdrRawSet:
            return QString("HDRI DNG · %1 brackets").arg(group.bracketCount());
        case MediaAssetKind::HdrCombinedSet:
            return QString("HDRI DNG + JPG · %1 brackets").arg(group.bracketCount());
        case MediaAssetKind::Video:
            return "VIDEO";
        case MediaAssetKind::SingleRaw:
            return "DNG";
        case MediaAssetKind::SinglePhoto:
        default:
            return "JPG";
    }
}

QString buildRangeTitle(const QList<CameraFileInfo>& files)
{
    if (files.isEmpty()) {
        return QString();
    }
    if (files.size() == 1) {
        return files.first().name;
    }

    const QFileInfo firstInfo(files.first().name);
    const QFileInfo lastInfo(files.last().name);
    const ParsedName firstParsed = parseName(files.first().name);
    const ParsedName lastParsed = parseName(files.last().name);

    if (firstParsed.hasNumber && lastParsed.hasNumber && firstParsed.prefix == lastParsed.prefix) {
        return QString("%1%2-%3")
            .arg(firstParsed.prefix)
            .arg(firstParsed.number)
            .arg(lastParsed.number);
    }

    return QString("%1 +%2")
        .arg(firstInfo.fileName())
        .arg(files.size() - 1);
}

MediaAssetGroup makeGroup(const QList<CameraFileInfo>& files, MediaAssetKind kind)
{
    MediaAssetGroup group;
    group.files = files;
    group.kind = kind;
    group.isHdrSet = (kind == MediaAssetKind::HdrJpegSet || kind == MediaAssetKind::HdrRawSet);
    group.isRaw = (kind == MediaAssetKind::HdrRawSet || kind == MediaAssetKind::SingleRaw);
    group.isVideo = (kind == MediaAssetKind::Video);
    group.representativeIndex = files.isEmpty() ? 0 : files.size() / 2;
    group.representative = files[group.representativeIndex];
    group.captureTime = group.representative.creationDate;
    group.id = group.representative.devicePath;
    group.displayTitle = group.isHdrSet ? buildRangeTitle(files) : group.representative.name;
    group.subtitle = kindBadge(group);
    return group;
}

QList<MediaAssetGroup> groupImageStream(const QList<CameraFileInfo>& input, bool raw)
{
    QList<CameraFileInfo> files;
    for (const CameraFileInfo& file : input) {
        if (!file.isVideo && file.isRaw == raw && isBrowsable(file)) {
            files.append(file);
        }
    }

    std::sort(files.begin(), files.end(), [](const CameraFileInfo& a, const CameraFileInfo& b) {
        const ParsedName aName = parseName(a.name);
        const ParsedName bName = parseName(b.name);
        if (aName.prefix == bName.prefix && aName.hasNumber && bName.hasNumber && aName.number != bName.number) {
            return aName.number < bName.number;
        }
        if (a.creationDate.isValid() && b.creationDate.isValid() && a.creationDate != b.creationDate) {
            return a.creationDate < b.creationDate;
        }
        return a.name < b.name;
    });

    QList<MediaAssetGroup> groups;
    QList<CameraFileInfo> current;

    const auto flushRun = [&groups, raw](QList<CameraFileInfo>& run) {
        if (run.isEmpty()) {
            return;
        }
        int start = 0;
        for (int i = 1; i <= run.size(); ++i) {
            if (i < run.size() && !startsNewNativeCapture(run, start, i)) continue;
            const QList<CameraFileInfo> capture = run.mid(start, i - start);
            if (capture.size() >= 3) {
                groups.append(makeGroup(capture, raw ? MediaAssetKind::HdrRawSet : MediaAssetKind::HdrJpegSet));
            } else {
                for (const CameraFileInfo& file : capture) {
                    groups.append(makeGroup({file}, raw ? MediaAssetKind::SingleRaw : MediaAssetKind::SinglePhoto));
                }
            }
            start = i;
        }
        run.clear();
    };

    for (const CameraFileInfo& file : files) {
        if (current.isEmpty()) {
            current.append(file);
            continue;
        }
        if (areAdjacentHdrCandidates(current, file)) {
            current.append(file);
            continue;
        }
        flushRun(current);
        current.append(file);
    }
    flushRun(current);

    return groups;
}

QList<MediaAssetGroup> groupVideos(const QList<CameraFileInfo>& input)
{
    QList<MediaAssetGroup> groups;
    for (const CameraFileInfo& file : input) {
        if (file.isVideo) {
            groups.append(makeGroup({file}, MediaAssetKind::Video));
        }
    }
    return groups;
}

} // namespace

QList<MediaAssetGroup> HdrGroupingService::buildGroups(const QList<CameraFileInfo>& files)
{
    QList<MediaAssetGroup> groups;
    QList<CameraFileInfo> enrichedFiles = files;
    inheritPairedJpegExposure(enrichedFiles);

    const QList<MediaAssetGroup> jpegGroups = groupImageStream(enrichedFiles, false);
    const QList<MediaAssetGroup> rawGroups = groupImageStream(enrichedFiles, true);
    const QList<MediaAssetGroup> videoGroups = groupVideos(enrichedFiles);

    groups.reserve(jpegGroups.size() + rawGroups.size() + videoGroups.size());
    groups.append(jpegGroups);
    groups.append(rawGroups);
    groups.append(videoGroups);

    QList<int> candidateCounts(groups.size(), 0);
    QList<int> linkedIndexes(groups.size(), -1);
    for (int i = 0; i < groups.size(); ++i) {
        if (groups[i].kind != MediaAssetKind::HdrRawSet) continue;
        for (int j = 0; j < groups.size(); ++j) {
            if (groups[j].kind != MediaAssetKind::HdrJpegSet || !sameHdrCapture(groups[i], groups[j])) {
                continue;
            }
            ++candidateCounts[i];
            ++candidateCounts[j];
            linkedIndexes[i] = j;
        }
    }
    QList<int> mergedRawIndexes;
    for (int i = 0; i < groups.size(); ++i) {
        const int j = linkedIndexes[i];
        // Fusionar solo parejas univocas, conservando el representativo JPG.
        if (candidateCounts[i] != 1 || j < 0 || candidateCounts[j] != 1) continue;
        MediaAssetGroup& jpeg = groups[j];
        jpeg.files.append(groups[i].files);
        jpeg.kind = MediaAssetKind::HdrCombinedSet;
        jpeg.subtitle = kindBadge(jpeg);
        mergedRawIndexes.append(i);
    }
    for (auto it = mergedRawIndexes.crbegin(); it != mergedRawIndexes.crend(); ++it) {
        groups.removeAt(*it);
    }

    std::sort(groups.begin(), groups.end(), [](const MediaAssetGroup& a, const MediaAssetGroup& b) {
        if (a.captureTime.isValid() && b.captureTime.isValid() && a.captureTime != b.captureTime) {
            return a.captureTime > b.captureTime;
        }
        return a.displayTitle > b.displayTitle;
    });

    int hdrJpegCount = 0;
    int hdrRawCount = 0;
    int hdrCombinedCount = 0;
    int singleCount = 0;
    for (const MediaAssetGroup& group : groups) {
        if (group.kind == MediaAssetKind::HdrJpegSet) {
            ++hdrJpegCount;
        } else if (group.kind == MediaAssetKind::HdrRawSet) {
            ++hdrRawCount;
        } else if (group.isCombinedHdr()) {
            ++hdrCombinedCount;
        } else {
            ++singleCount;
        }
    }
    LOGI("hdr") << "Catalogo agrupado en" << groups.size()
                << "items. HDR JPG=" << hdrJpegCount
                << "HDR DNG=" << hdrRawCount
                << "HDR DNG+JPG=" << hdrCombinedCount
                << "otros=" << singleCount;

    return groups;
}

QList<MediaAssetGroup> HdrGroupingService::downloadGroups(const MediaAssetGroup& group)
{
    if (!group.isCombinedHdr()) return {group};

    // Recuperar cada formato con su propia fecha mantiene las carpetas ya descargadas.
    QList<CameraFileInfo> jpegFiles, rawFiles;
    for (const CameraFileInfo& file : group.files) {
        (file.isRaw ? rawFiles : jpegFiles).append(file);
    }
    QList<MediaAssetGroup> result;
    if (!jpegFiles.isEmpty()) result.append(makeGroup(jpegFiles, MediaAssetKind::HdrJpegSet));
    if (!rawFiles.isEmpty()) result.append(makeGroup(rawFiles, MediaAssetKind::HdrRawSet));
    return result;
}
