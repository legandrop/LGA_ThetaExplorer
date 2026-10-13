#include "thetaexplorer/ThetaCameraService.h"
#include "thetaexplorer/ThetaBridge.h"
#include "thetaexplorer/Logger.h"
#include "thetaexplorer/HdrGroupingService.h"
#include "thetaexplorer/CameraMediaCache.h"
#include "thetaexplorer/AppPaths.h"
#include <QSet>
#include <QTimer>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QPointer>
#include <algorithm>
#include <utility>

namespace {
QString previewKey(const CameraFileInfo& file)
{
    return file.devicePath + '|' + file.name + '|' + QString::number(file.sizeBytes) + '|'
        + file.creationDate.toUTC().toString(Qt::ISODateWithMs) + '|' + file.persistentId;
}
bool sameFile(const CameraFileInfo& a, const CameraFileInfo& b)
{
    return a.devicePath == b.devicePath && a.name == b.name && a.sizeBytes == b.sizeBytes
        && a.creationDate == b.creationDate && a.persistentId == b.persistentId;
}
bool mediaFile(const CameraFileInfo& file)
{
    const QString extension = QFileInfo(file.name).suffix().toLower();
    return file.isRaw || file.isVideo || extension == "jpg" || extension == "jpeg"
        || extension == "dng" || extension == "arw" || extension == "cr2" || extension == "nef"
        || extension == "mp4" || extension == "mov";
}
}

ThetaCameraService::ThetaCameraService(QObject* parent)
    : QObject(parent)
{
    m_bridge = new ThetaBridge(this);
    m_mediaCache = std::make_unique<CameraMediaCache>(AppPaths::mediaCacheDirectory());

    connect(m_bridge, &ThetaBridge::cameraIdentityReady, this, [this](const QString& identifier) {
        if (identifier != m_cameraIdentifier) {
            m_files.clear();
            m_catalogComplete = false;
            m_pendingThumbnails.clear(); m_thumbnailQueue.clear();
            m_rawThumbnailCache.clear(); m_rawFallbackAttempted.clear();
            m_previewCache.clear(); m_previewCacheOrder.clear();
            m_pendingPreview = CameraFileInfo{};
            m_imageInfoQueue.clear(); m_imageInfoActive = false;
            m_catalogEnrichmentPending = false;
            ++m_imageInfoRequestId;
            if (m_imageInfoTimer) m_imageInfoTimer->stop();
        }
        m_cameraIdentifier = identifier;
    });

    connect(m_bridge, &ThetaBridge::cameraConnected,
            this, [this](const QString& name) {
                m_connected = true;
                emit cameraConnected(name);
            });

    connect(m_bridge, &ThetaBridge::cameraDisconnected,
            this, [this]() {
                m_connected = false;
                m_cameraIdentifier.clear();
                m_catalogComplete = false;
                m_pendingThumbnails.clear();
                m_files.clear();
                if (m_thumbnailTimer) m_thumbnailTimer->stop();
                m_thumbnailQueue.clear();
                m_imageInfoQueue.clear();
                m_rawThumbnailCache.clear();
                m_rawFallbackAttempted.clear();
                m_imageInfoActive = false;
                m_catalogEnrichmentPending = false;
                m_headerRecovery = false;
                ++m_imageInfoRequestId;
                if (m_imageInfoTimer) m_imageInfoTimer->stop();
                m_downloadQueue.clear();
                m_pendingPreview = CameraFileInfo{};
                m_previewCache.clear();
                m_previewCacheOrder.clear();
                m_previewDirectory.reset();
                m_activeDownload = PendingDownload{};
                m_downloadActive = false;
                m_deleteActive = false;
                m_waitingForDeleteVerification = false;
                m_pendingDeleteFiles.clear();
                if (m_deleteTimer) m_deleteTimer->stop();
                m_recoveringAfterDownloadError = false;
                emit batteryLevelChanged(-1, false);
                emit cameraDisconnected();
            });

    connect(m_bridge, &ThetaBridge::catalogReady, this, [this](const QString& camera, const auto& files, bool complete) {
        if (camera != m_cameraIdentifier || !m_connected) return;
        m_catalogComplete = complete;
        onFileListUpdated(files);
    });
    connect(m_bridge, &ThetaBridge::thumbnailResult, this, &ThetaCameraService::acceptThumbnail);
    connect(m_bridge, &ThetaBridge::imageInfoReady, this,
            [this](const CameraFileInfo& file, const QPixmap& thumbnail, quint64 id) {
        finishImageInfo(file, thumbnail, QString(), id);
    });
    connect(m_bridge, &ThetaBridge::imageInfoFailed, this,
            [this](const QString& path, const QString& error, quint64 id) {
        if (path == m_activeImageInfo.devicePath) finishImageInfo(m_activeImageInfo, QPixmap(), error, id);
    });

    connect(m_bridge, &ThetaBridge::batteryLevelChanged,
            this, &ThetaCameraService::batteryLevelChanged);

    connect(m_bridge, &ThetaBridge::downloadProgress,
            this, &ThetaCameraService::onBridgeDownloadProgress);

    connect(m_bridge, &ThetaBridge::downloadFileCompleted,
            this, &ThetaCameraService::onBridgeDownloadFileCompleted);

    connect(m_bridge, &ThetaBridge::downloadError,
            this, &ThetaCameraService::onBridgeDownloadError);

    connect(m_bridge, &ThetaBridge::filesDeleted, this, [this](const QString& camera, const auto& files) {
        if (!m_deleteActive || camera != m_cameraIdentifier || camera != m_pendingDeleteCameraIdentifier) return;
        QStringList paths;
        for (const auto& file : files) {
            const bool requested = std::any_of(m_pendingDeleteFiles.cbegin(), m_pendingDeleteFiles.cend(),
                [&file](const auto& selected) { return sameFile(file, selected); });
            if (requested && currentFile(file) && !paths.contains(file.devicePath)) paths.append(file.devicePath);
        }
        if (!paths.isEmpty()) onBridgeDeleteCompleted(paths);
    });

    connect(m_bridge, &ThetaBridge::deleteError,
            this, &ThetaCameraService::onBridgeDeleteError);

    connect(m_bridge, &ThetaBridge::cameraError,
            this, [this](const QString& msg) {
                emit errorOccurred("Camera error: " + msg);
            });

    // Thumbnail drain timer
    m_thumbnailTimer = new QTimer(this);
    m_thumbnailTimer->setInterval(60); // ms between batches
    connect(m_thumbnailTimer, &QTimer::timeout,
            this, &ThetaCameraService::drainThumbnailQueue);

    m_deleteTimer = new QTimer(this);
    m_deleteTimer->setSingleShot(true);
    connect(m_deleteTimer, &QTimer::timeout,
            this, &ThetaCameraService::onDeleteTimedOut);
    m_imageInfoTimer = new QTimer(this);
    m_imageInfoTimer->setSingleShot(true);
    connect(m_imageInfoTimer, &QTimer::timeout, this, [this] {
        if (!m_imageInfoActive) return;
        // Un timeout no cancela ICC: cerrar la sesion antes de enviar mas operaciones.
        m_headerRecovery = true;
        m_recoveringAfterDownloadError = true;
        finishImageInfo(m_activeImageInfo, QPixmap(), "The image header request timed out.", m_imageInfoRequestId);
        m_bridge->recoverDownloadSession();
    });
}

ThetaCameraService::~ThetaCameraService()
{
    stop();
}

void ThetaCameraService::start()
{
    LOGI("service") << "Starting camera browsing";
    m_bridge->startBrowsing();
}

void ThetaCameraService::stop()
{
    m_cameraIdentifier.clear();
    m_catalogComplete = false;
    m_pendingThumbnails.clear();
    if (m_thumbnailTimer) m_thumbnailTimer->stop();
    if (m_imageInfoTimer) m_imageInfoTimer->stop();
    m_imageInfoQueue.clear();
    m_imageInfoActive = false;
    m_catalogEnrichmentPending = false;
    m_headerRecovery = false;
    ++m_imageInfoRequestId;
    m_downloadQueue.clear();
    m_pendingPreview = CameraFileInfo{};
    m_activeDownload = PendingDownload{};
    m_downloadActive = false;
    m_deleteActive = false;
    m_waitingForDeleteVerification = false;
    m_pendingDeleteFiles.clear();
    if (m_deleteTimer) m_deleteTimer->stop();
    m_recoveringAfterDownloadError = false;
    m_bridge->stopBrowsing();
}

void ThetaCameraService::refresh()
{
    LOGI("service") << "Refreshing camera browsing";
    if (m_thumbnailTimer) m_thumbnailTimer->stop();
    m_thumbnailQueue.clear();
    m_pendingThumbnails.clear();
    m_bridge->refreshCatalog();
}

void ThetaCameraService::onFileListUpdated(const QList<CameraFileInfo>& files)
{
    // The bridge already debounces (300ms GCD) so this should fire only once
    // per connection burst. We always accept the new list.
    LOGI("service") << "fileListUpdated:" << files.size() << "files";
    m_pendingThumbnails.clear();

    QList<CameraFileInfo> updated = files;
    QSet<QString> paths;
    for (const auto& file : updated) {
        if (file.devicePath.isEmpty() || file.name.isEmpty() || paths.contains(file.devicePath)) {
            m_catalogComplete = false;
            LOGW("service") << "Se descartó un catálogo con rutas vacías o ambiguas";
            return;
        }
        paths.insert(file.devicePath);
    }
    if (m_catalogComplete && m_cameraIdentifier.startsWith("serial:")
        && !m_cameraIdentifier.mid(7).trimmed().isEmpty()
        && std::all_of(updated.cbegin(), updated.cend(), [this](const auto& file) { return !mediaFile(file) || cacheEligible(file); })) {
        if (!m_mediaCache->reconcile(m_cameraIdentifier, updated)) warnCacheFailure();
    }
    for (CameraFileInfo& file : updated) {
        const auto old = std::find_if(m_files.cbegin(), m_files.cend(), [&file](const CameraFileInfo& previous) {
            return sameFile(previous, file);
        });
        if (old != m_files.cend()) {
            file.exposureMode = old->exposureMode; file.exposureValue = old->exposureValue;
            file.exposureTime = old->exposureTime;
            file.width = old->width; file.height = old->height;
        } else {
            // Una ruta reutilizada no conserva el preview de otro objeto de la camara.
            m_rawThumbnailCache.remove(file.devicePath);
            m_rawFallbackAttempted.remove(file.devicePath);
        }
        if (cacheEligible(file)) m_mediaCache->enrichMetadata(m_cameraIdentifier, file);
    }
    m_files = updated;
    if (m_catalogComplete && m_pendingDeleteCameraIdentifier == m_cameraIdentifier
        && (m_deleteActive || m_waitingForDeleteVerification)) {
        QSet<QString> remainingPaths;
        remainingPaths.reserve(m_files.size());
        for (const CameraFileInfo& file : m_files) {
            remainingPaths.insert(file.devicePath);
        }

        QStringList deletedPaths;
        QStringList stillPresentNames;
        for (const CameraFileInfo& file : m_pendingDeleteFiles) {
            if (!remainingPaths.contains(file.devicePath)) {
                deletedPaths << file.devicePath;
            } else {
                stillPresentNames << file.name;
            }
        }

        if (!deletedPaths.isEmpty()) {
            QList<CameraFileInfo> deletedFiles;
            for (const auto& file : m_pendingDeleteFiles)
                if (deletedPaths.contains(file.devicePath) && cacheEligible(file)) deletedFiles.append(file);
            if (!deletedFiles.isEmpty() && !m_mediaCache->removeFiles(m_cameraIdentifier, deletedFiles)) warnCacheFailure();
            LOGI("service") << "Verified deleted files via refreshed catalog:"
                            << deletedPaths;
            m_deleteActive = false;
            m_waitingForDeleteVerification = false;
            if (m_deleteTimer) m_deleteTimer->stop();
            m_pendingDeleteFiles.clear();
            emit deleteCompleted(deletedPaths);
            kickNextDownload();
        } else if (m_waitingForDeleteVerification) {
            const QString detail = stillPresentNames.isEmpty()
                ? QString("Delete verification timed out.")
                : QString("Delete did not complete for: %1").arg(stillPresentNames.join(", "));
            LOGW("service") << detail;
            m_deleteActive = false;
            m_waitingForDeleteVerification = false;
            if (m_deleteTimer) m_deleteTimer->stop();
            m_pendingDeleteFiles.clear();
            emit errorOccurred("Delete error: " + detail);
            kickNextDownload();
        }
    }

    if (m_recoveringAfterDownloadError) {
        LOGI("service") << "Catalog refreshed during download recovery";
        QTimer::singleShot(250, this, &ThetaCameraService::resumeAfterRecovery);
    }

    // Stop any in-progress thumbnail fetching — stale handles after re-enumeration
    if (m_thumbnailTimer) m_thumbnailTimer->stop();
    m_thumbnailQueue.clear();

    m_imageInfoQueue.clear();
    const bool headerRecovery = m_headerRecovery;
    if (!headerRecovery) {
        m_rawFallbackAttempted.clear();
        m_rawThumbnailCache.clear();
        m_catalogMetadataFailed = false;
    }
    QList<CameraFileInfo> singles;
    for (const MediaAssetGroup& group : HdrGroupingService::buildGroups(m_files)) {
        if (group.kind == MediaAssetKind::SinglePhoto) singles.append(group.representative);
    }
    std::sort(singles.begin(), singles.end(), [](const CameraFileInfo& a, const CameraFileInfo& b) {
        return a.name.toUpper() < b.name.toUpper();
    });
    static const QRegularExpression numbered("^(.*?)(\\d+)\\.(?:jpe?g)$", QRegularExpression::CaseInsensitiveOption);
    QSet<QString> candidates;
    for (int i = 1; i < singles.size(); ++i) {
        const CameraFileInfo& a = singles[i - 1]; const CameraFileInfo& b = singles[i];
        const auto an = numbered.match(a.name); const auto bn = numbered.match(b.name);
        if (!an.hasMatch() || !bn.hasMatch() || an.captured(1).compare(bn.captured(1), Qt::CaseInsensitive) != 0
            || bn.captured(2).toLongLong() - an.captured(2).toLongLong() != 1
            || a.devicePath.isEmpty() || b.devicePath.isEmpty()
            || QDir::cleanPath(QFileInfo(a.devicePath).path()) != QDir::cleanPath(QFileInfo(b.devicePath).path())
            || !a.creationDate.isValid() || !b.creationDate.isValid()) continue;
        const qint64 gap = a.creationDate.msecsTo(b.creationDate);
        if (gap <= 2500 || gap > 5 * 60 * 1000) continue;
        for (const CameraFileInfo& candidate : {a, b}) {
            if (!headerRecovery && candidate.exposureMode < 0 && !candidates.contains(candidate.devicePath)) {
                candidates.insert(candidate.devicePath); m_imageInfoQueue.append(candidate);
            }
        }
    }
    m_catalogEnrichmentPending = !m_imageInfoQueue.isEmpty();
    m_headerRecovery = false;
    if (!m_catalogEnrichmentPending) publishCatalog();
    m_thumbnailTimer->start();
}

void ThetaCameraService::publishCatalog()
{
    m_catalogEnrichmentPending = false;
    emit fileListReady(m_files);
    m_thumbnailQueue = m_files;
    if (m_catalogMetadataFailed) emit errorOccurred("Some bracket metadata could not be read. Those photos remain separate.");
}

void ThetaCameraService::finishImageInfo(const CameraFileInfo& file, const QPixmap& thumbnail,
                                        const QString& error, quint64 requestId)
{
    if (!m_imageInfoActive || requestId != m_imageInfoRequestId || !sameFile(file, m_activeImageInfo)
        || m_activeImageInfoCameraIdentifier != m_cameraIdentifier) return;
    m_imageInfoTimer->stop();
    m_imageInfoActive = false;
    const auto current = std::find_if(m_files.begin(), m_files.end(), [&file](const CameraFileInfo& value) {
        return sameFile(value, file) && value.nativeFileHandle == file.nativeFileHandle;
    });
    if (current != m_files.end()) {
        if (error.isEmpty()) {
            current->exposureMode = file.exposureMode; current->exposureValue = file.exposureValue;
            current->exposureTime = file.exposureTime;
            current->width = file.width; current->height = file.height;
            if (cacheEligible(*current) && !m_mediaCache->storeMetadata(m_cameraIdentifier, *current)) warnCacheFailure();
            if (!thumbnail.isNull()) {
                if (cacheEligible(*current) && !m_mediaCache->storeThumbnail(m_cameraIdentifier, *current, thumbnail)) warnCacheFailure();
                if (m_rawThumbnailCache.size() >= 8 && !m_rawThumbnailCache.contains(file.devicePath)) m_rawThumbnailCache.erase(m_rawThumbnailCache.begin());
                m_rawThumbnailCache.insert(file.devicePath, thumbnail);
                emit thumbnailReady(file.devicePath, thumbnail);
            }
        } else {
            LOGW("service") << "No se pudo leer la cabecera de" << file.name << error;
            if (file.isRaw) emit errorOccurred("A RAW preview could not be loaded. Refresh the camera to retry.");
            else m_catalogMetadataFailed = true;
        }
    }
    emit previewActivityChanged(false);
    if (m_catalogEnrichmentPending && m_imageInfoQueue.isEmpty() && !m_headerRecovery) publishCatalog();
    kickNextDownload();
    m_thumbnailTimer->start();
}

void ThetaCameraService::drainThumbnailQueue()
{
    // La copia del original necesita el mismo canal USB que las miniaturas.
    if (m_downloadActive || m_deleteActive || m_recoveringAfterDownloadError || m_imageInfoActive) return;
    if (!m_catalogEnrichmentPending && !m_downloadQueue.isEmpty()) { kickNextDownload(); return; }
    if (!m_imageInfoQueue.isEmpty()) {
        m_activeImageInfo = m_imageInfoQueue.takeFirst();
        m_activeImageInfoCameraIdentifier = m_cameraIdentifier;
        m_imageInfoActive = true;
        ++m_imageInfoRequestId;
        emit previewActivityChanged(true);
        m_imageInfoTimer->start(15000);
        m_bridge->requestImageInfo(m_activeImageInfo, m_imageInfoRequestId);
        return;
    }
    if (m_catalogEnrichmentPending) publishCatalog();
    // Request 3 thumbnails per tick to avoid overwhelming the camera
    for (int i = 0; i < 3 && !m_thumbnailQueue.isEmpty(); ++i) {
        CameraFileInfo fi = m_thumbnailQueue.takeFirst();
        requestThumbnail(fi);
    }
    if (m_thumbnailQueue.isEmpty()) {
        m_thumbnailTimer->stop();
        LOGD("service") << "All thumbnails requested";
    }
}

void ThetaCameraService::requestThumbnail(const CameraFileInfo& file)
{
    if (!m_connected || !currentFile(file)) return;
    if (cacheEligible(file)) {
        const QPixmap image = m_mediaCache->thumbnail(m_cameraIdentifier, file);
        if (!image.isNull()) { emit thumbnailReady(file.devicePath, image); return; }
    }
    if (m_rawThumbnailCache.contains(file.devicePath)) {
        emit thumbnailReady(file.devicePath, m_rawThumbnailCache.value(file.devicePath)); return;
    }
    if (isDownloadActive() || m_deleteActive) {
        m_thumbnailQueue.removeIf([&file](const CameraFileInfo& queued) { return queued.devicePath == file.devicePath; });
        m_thumbnailQueue.prepend(file);
        m_thumbnailTimer->start();
        return;
    }
    for (const auto& pending : std::as_const(m_pendingThumbnails))
        if (pending.cameraIdentifier == m_cameraIdentifier && sameFile(pending.file, file)) return;
    const quint64 requestId = ++m_thumbnailRequestId;
    m_pendingThumbnails.insert(requestId, {file, m_cameraIdentifier});
    m_bridge->requestThumbnail(file, requestId);
}

bool ThetaCameraService::currentFile(const CameraFileInfo& file) const
{
    return std::any_of(m_files.cbegin(), m_files.cend(), [&file](const auto& current) { return sameFile(current, file); });
}

bool ThetaCameraService::cacheEligible(const CameraFileInfo& file) const
{
    return m_cameraIdentifier.startsWith("serial:") && !m_cameraIdentifier.mid(7).trimmed().isEmpty()
        && mediaFile(file)
        && !file.devicePath.isEmpty() && !file.name.isEmpty() && !file.name.contains('/') && !file.name.contains('\\')
        && file.name != "." && file.name != ".." && file.sizeBytes > 0
        && (file.creationDate.isValid() || !file.persistentId.isEmpty());
}

void ThetaCameraService::warnCacheFailure()
{
    if (m_cacheWarningShown) return;
    m_cacheWarningShown = true;
    LOGW("service") << "No se pudo actualizar la cache local; se mantiene la transferencia actual:"
                    << m_mediaCache->errorString();
}

void ThetaCameraService::acceptThumbnail(const CameraFileInfo& file, const QPixmap& thumbnail, quint64 requestId)
{
    const auto found = m_pendingThumbnails.find(requestId);
    if (found == m_pendingThumbnails.end()) return;
    const PendingThumbnail pending = found.value();
    m_pendingThumbnails.erase(found);
    if (pending.cameraIdentifier != m_cameraIdentifier || !sameFile(pending.file, file) || !currentFile(file)) return;
    if (!thumbnail.isNull()) {
        if (cacheEligible(file) && !m_mediaCache->storeThumbnail(m_cameraIdentifier, file, thumbnail)) warnCacheFailure();
        emit thumbnailReady(file.devicePath, thumbnail);
    } else if (file.isRaw && !m_rawFallbackAttempted.contains(file.devicePath)) {
        m_rawFallbackAttempted.insert(file.devicePath);
        m_imageInfoQueue.append(file);
        m_thumbnailTimer->start();
    }
}

void ThetaCameraService::requestPreview(const CameraFileInfo& file)
{
    if (!m_connected || file.isRaw || file.isVideo) return;
    const auto found = std::find_if(m_files.cbegin(), m_files.cend(), [&file](const CameraFileInfo& candidate) {
        return candidate.devicePath == file.devicePath && candidate.name == file.name
            && candidate.sizeBytes == file.sizeBytes && candidate.creationDate == file.creationDate
            && candidate.persistentId == file.persistentId;
    });
    if (found == m_files.cend()) return;
    const CameraFileInfo current = *found;
    const QString extension = QFileInfo(current.name).suffix().toLower();
    if ((extension != "jpg" && extension != "jpeg") || current.name.contains('/')
        || current.name.contains('\\') || current.name.isEmpty()) return;

    // Una sola pendiente: navegar rapido no descarga todos los items intermedios.
    m_pendingPreview = CameraFileInfo{};
    if (cacheEligible(current)) {
        const QString path = m_mediaCache->previewFile(m_cameraIdentifier, current);
        if (!path.isEmpty()) { emit previewFileReady(current.devicePath, path); return; }
    }
    const QString key = previewKey(current);
    const QString cached = m_previewCache.value(key);
    if (!cached.isEmpty() && QFileInfo(cached).size() == current.sizeBytes) {
        emit previewFileReady(current.devicePath, cached);
        return;
    }
    if (m_downloadActive && m_activeDownload.preview
        && previewKey(m_activeDownload.file) == key) return;
    m_pendingPreview = current;
    kickNextDownload();
}

void ThetaCameraService::downloadFiles(const QList<CameraFileInfo>& files,
                                        const QString& destinationPath)
{
    for (const CameraFileInfo& file : files) {
        m_downloadQueue.append(PendingDownload{file, destinationPath, 1, false, m_cameraIdentifier});
        LOGD("service") << "Enqueued download candidate:"
                        << file.name
                        << "destDir:" << destinationPath
                        << "sizeBytes:" << file.sizeBytes
                        << "created:" << file.creationDate.toString(Qt::ISODate);
    }

    LOGI("service") << "Queued" << files.size() << "downloads for"
                    << destinationPath << "pending total:" << m_downloadQueue.size();
    if (!m_downloadActive) {
        kickNextDownload();
    }
}

void ThetaCameraService::deleteFiles(const QList<CameraFileInfo>& files)
{
    if (isDownloadActive() || m_deleteActive) {
        emit errorOccurred("Wait for the current camera transfer to finish before deleting files.");
        return;
    }
    m_pendingDeleteFiles = files;
    m_pendingDeleteCameraIdentifier = m_cameraIdentifier;
    m_deleteActive = !files.isEmpty();
    m_waitingForDeleteVerification = false;
    if (m_deleteTimer) {
        m_deleteTimer->start(m_deleteTimeoutMs);
    }
    QStringList names;
    for (const CameraFileInfo& file : files) {
        names << file.name;
    }
    LOGI("service") << "Delete requested for" << files.size()
                    << "file(s):" << names;
    m_bridge->requestDeleteFiles(files);
}

void ThetaCameraService::onBridgeDownloadProgress(const QString& fileName, int percent)
{
    if (!m_downloadActive || m_activeDownload.preview || fileName != m_activeDownload.file.name) return;
    emit downloadProgress(fileName, percent);
}

void ThetaCameraService::onBridgeDownloadFileCompleted(const QString& fileName,
                                                       const QString& savedPath)
{
    if (!m_downloadActive) return;
    if (fileName != m_activeDownload.file.name) {
        onBridgeDownloadError(m_activeDownload.file.name, "The downloaded file does not match the requested file.");
        return;
    }
    if (m_activeDownload.preview) {
        const PendingDownload completed = m_activeDownload;
        const CameraFileInfo file = m_activeDownload.file;
        const QString destination = m_activeDownload.destinationPath;
        // ImageCaptureCore puede devolver solo ICSavedFilename, relativo al destino.
        const QString resolvedPath = QDir::isAbsolutePath(savedPath) ? savedPath
            : QDir(destination).filePath(savedPath.isEmpty() ? file.name : savedPath);
        const QFileInfo received(resolvedPath);
        const QString expected = QFileInfo(QDir(destination).filePath(file.name)).canonicalFilePath();
        const bool valid = completed.cameraIdentifier == m_cameraIdentifier && currentFile(file)
            && !expected.isEmpty() && received.isFile() && received.canonicalFilePath() == expected
            && received.fileName() == file.name && received.size() == file.sizeBytes;
        m_downloadActive = false;
        m_activeDownload = PendingDownload{};
        if (valid) {
            QString readyPath = received.canonicalFilePath();
            if (completed.persistentPreview) {
                if (m_mediaCache->commitPreview(m_cameraIdentifier, file, readyPath))
                    readyPath = m_mediaCache->previewFile(m_cameraIdentifier, file);
                else warnCacheFailure();
            } else {
                const QString key = previewKey(file);
                m_previewCache.insert(key, readyPath);
                m_previewCacheOrder.removeAll(key);
                m_previewCacheOrder.append(key);
                while (m_previewCacheOrder.size() > 8) {
                    const QString expired = m_previewCacheOrder.takeFirst();
                    QFile::remove(m_previewCache.take(expired));
                }
            }
            emit previewFileReady(file.devicePath, readyPath);
        } else {
            LOGW("service") << "La copia de preview no coincide con el archivo solicitado:" << file.name;
            emit previewError(file.devicePath, "The camera preview could not be read.");
        }
        emit previewActivityChanged(false);
        QTimer::singleShot(m_interDownloadDelayMs, this, &ThetaCameraService::kickNextDownload);
        return;
    }
    const PendingDownload completed = m_activeDownload;
    const QString resolvedPath = QDir::isAbsolutePath(savedPath) ? savedPath
        : QDir(completed.destinationPath).filePath(savedPath.isEmpty() ? completed.file.name : savedPath);
    const QFileInfo received(resolvedPath);
    const QString canonicalPath = received.canonicalFilePath();
    const QString expectedPath = QFileInfo(QDir(completed.destinationPath).filePath(completed.file.name)).canonicalFilePath();
    if (completed.cameraIdentifier != m_cameraIdentifier || !received.isFile()
        || expectedPath.isEmpty() || canonicalPath != expectedPath
        || received.fileName() != completed.file.name || received.size() != completed.file.sizeBytes) {
        onBridgeDownloadError(fileName, "The downloaded file could not be verified against the requested file.");
        return;
    }
    CameraFileInfo verifiedFile = completed.file;
    verifiedFile.nativeFileHandle = nullptr;
    // Entregar el DTO verificado antes de que el flujo legado cierre el batch.
    const QPointer<ThetaCameraService> alive(this);
    emit userDownloadVerified(completed.cameraIdentifier, verifiedFile, canonicalPath);
    if (!alive) return;
    if (!m_activeDownload.file.devicePath.isEmpty()) {
        m_failureCountByDevicePath.remove(m_activeDownload.file.devicePath);
    }
    m_downloadActive = false;
    m_activeDownload = PendingDownload{};
    emit downloadFileCompleted(fileName, canonicalPath);
    QTimer::singleShot(m_interDownloadDelayMs, this, &ThetaCameraService::kickNextDownload);
}

void ThetaCameraService::onBridgeDownloadError(const QString& fileName,
                                               const QString& errorMessage)
{
    if (!m_downloadActive || fileName != m_activeDownload.file.name) return;
    if (m_activeDownload.preview) {
        const QString path = m_activeDownload.file.devicePath;
        LOGW("service") << "Fallo al copiar el JPEG para preview:" << fileName << errorMessage;
        m_downloadActive = false;
        m_activeDownload = PendingDownload{};
        emit previewError(path, "The camera preview could not be loaded. Select the image again to retry.");
        emit previewActivityChanged(false);
        QTimer::singleShot(m_postErrorDelayMs, this, &ThetaCameraService::kickNextDownload);
        return;
    }
    const QString devicePath = m_activeDownload.file.devicePath;
    const int failureCount = devicePath.isEmpty()
        ? 0
        : (m_failureCountByDevicePath.value(devicePath, 0) + 1);
    if (!devicePath.isEmpty()) {
        m_failureCountByDevicePath.insert(devicePath, failureCount);
    }

    LOGW("service") << "Download failed:"
                    << fileName
                    << "destDir:" << m_activeDownload.destinationPath
                    << "attempt:" << m_activeDownload.attempt
                    << "failureCount:" << failureCount
                    << "sizeBytes:" << m_activeDownload.file.sizeBytes
                    << "devicePath:" << m_activeDownload.file.devicePath
                    << "message:" << errorMessage;

    const bool isPipelineError = errorMessage.contains("-9934");
    const bool canRecover = isPipelineError
        && m_activeDownload.attempt < m_maxRecoveryRetries
        && !m_recoveringAfterDownloadError;

    if (canRecover) {
        PendingDownload retry = m_activeDownload;
        retry.attempt += 1;
        m_downloadQueue.prepend(retry);
        m_downloadActive = false;
        m_activeDownload = PendingDownload{};
        m_recoveringAfterDownloadError = true;
        LOGW("service") << "Will recover session and retry file:"
                        << retry.file.name
                        << "nextAttempt:" << retry.attempt
                        << "failureCount:" << failureCount;
        m_bridge->recoverDownloadSession();
        return;
    }

    emit downloadError(fileName, errorMessage);

    m_downloadActive = false;
    m_activeDownload = PendingDownload{};
    QTimer::singleShot(m_postErrorDelayMs, this, &ThetaCameraService::kickNextDownload);
}

void ThetaCameraService::resumeAfterRecovery()
{
    if (!m_recoveringAfterDownloadError) {
        return;
    }

    LOGI("service") << "Resuming queue after download recovery";
    m_recoveringAfterDownloadError = false;
    if (!m_downloadActive) {
        QTimer::singleShot(m_postErrorDelayMs, this, &ThetaCameraService::kickNextDownload);
    }
}

void ThetaCameraService::kickNextDownload()
{
    if (m_downloadActive || m_deleteActive || m_recoveringAfterDownloadError || m_imageInfoActive || m_catalogEnrichmentPending) {
        return;
    }

    if (!m_downloadQueue.isEmpty()) {
        m_activeDownload = m_downloadQueue.takeFirst();
        const CameraFileInfo requested = m_activeDownload.file;
        if (m_activeDownload.cameraIdentifier != m_cameraIdentifier) {
            m_activeDownload = PendingDownload{};
            emit downloadError(requested.name, "The connected camera changed. Start the download again.");
            QTimer::singleShot(0, this, &ThetaCameraService::kickNextDownload);
            return;
        }
        const auto current = std::find_if(m_files.cbegin(), m_files.cend(), [&requested](const CameraFileInfo& file) {
            return file.devicePath == requested.devicePath && file.name == requested.name
                && file.sizeBytes == requested.sizeBytes && file.creationDate == requested.creationDate
                && file.persistentId == requested.persistentId;
        });
        if (current == m_files.cend()) {
            m_activeDownload = PendingDownload{};
            emit downloadError(requested.name, "The camera file has changed or is no longer available. Refresh and try again.");
            QTimer::singleShot(0, this, &ThetaCameraService::kickNextDownload);
            return;
        }
        // Una sesion recuperada puede renovar ICCameraFile aunque la toma sea la misma.
        m_activeDownload.file.nativeFileHandle = current->nativeFileHandle;
    } else if (!m_pendingPreview.devicePath.isEmpty()) {
        const CameraFileInfo requested = m_pendingPreview;
        m_pendingPreview = CameraFileInfo{};
        const auto current = std::find_if(m_files.cbegin(), m_files.cend(), [&requested](const CameraFileInfo& candidate) {
            return candidate.devicePath == requested.devicePath && candidate.name == requested.name
                && candidate.sizeBytes == requested.sizeBytes && candidate.creationDate == requested.creationDate
                && candidate.persistentId == requested.persistentId;
        });
        if (current == m_files.cend()) return;
        const CameraFileInfo file = *current;
        QString directory;
        if (cacheEligible(file)) {
            directory = m_mediaCache->preparePreviewDirectory(m_cameraIdentifier, file);
            if (directory.isEmpty()) warnCacheFailure();
        }
        const bool persistent = !directory.isEmpty();
        if (!persistent) {
            if (!m_previewDirectory) m_previewDirectory = std::make_unique<QTemporaryDir>(
                QDir::tempPath() + "/ThetaExplorer-preview-XXXXXX");
            directory = m_previewDirectory->path() + '/' + QString::fromLatin1(
                QCryptographicHash::hash(previewKey(file).toUtf8(), QCryptographicHash::Sha256).toHex());
            if (!m_previewDirectory->isValid() || !QDir().mkpath(directory)) {
                LOGW("service") << "No se pudo crear el destino temporal del preview";
                emit previewError(file.devicePath, "The preview cache could not be created.");
                return;
            }
        }
        m_activeDownload = PendingDownload{file, directory, 1, true, m_cameraIdentifier, persistent};
    } else {
        return;
    }
    m_downloadActive = true;
    if (m_activeDownload.preview) emit previewActivityChanged(true);
    LOGD("service") << "Starting serial download:" << m_activeDownload.file.name
                    << "destDir:" << m_activeDownload.destinationPath
                    << "attempt:" << m_activeDownload.attempt
                    << "remaining queue:" << m_downloadQueue.size();
    m_bridge->requestDownloadFile(m_activeDownload.file, m_activeDownload.destinationPath);
}

void ThetaCameraService::onBridgeDeleteCompleted(const QStringList& deletedPaths)
{
    QList<CameraFileInfo> deletedFiles;
    for (const auto& file : m_pendingDeleteFiles)
        if (deletedPaths.contains(file.devicePath) && cacheEligible(file)) deletedFiles.append(file);
    if (!deletedFiles.isEmpty() && !m_mediaCache->removeFiles(m_pendingDeleteCameraIdentifier, deletedFiles)) warnCacheFailure();
    if (m_deleteTimer) m_deleteTimer->stop();
    m_deleteActive = false;
    m_waitingForDeleteVerification = false;
    m_pendingDeleteFiles.clear();
    const QSet<QString> removed(deletedPaths.cbegin(), deletedPaths.cend());
    // Retirar tambien las lecturas pendientes: sus handles dejan de existir tras borrar.
    const auto wasRemoved = [&removed](const CameraFileInfo& file) { return removed.contains(file.devicePath); };
    for (const CameraFileInfo& file : m_files) {
        if (!wasRemoved(file)) continue;
        const QString key = previewKey(file);
        const QString cachedPreview = m_previewCache.take(key);
        if (!cachedPreview.isEmpty()) QFile::remove(cachedPreview);
        m_previewCacheOrder.removeAll(key);
        m_rawThumbnailCache.remove(file.devicePath);
        m_rawFallbackAttempted.remove(file.devicePath);
    }
    m_files.removeIf(wasRemoved);
    m_thumbnailQueue.removeIf(wasRemoved);
    m_imageInfoQueue.removeIf(wasRemoved);
    m_downloadQueue.removeIf([&removed](const PendingDownload& item) { return removed.contains(item.file.devicePath); });
    if (wasRemoved(m_pendingPreview)) m_pendingPreview = CameraFileInfo{};
    LOGI("service") << "Delete completed for paths:" << deletedPaths;
    emit deleteCompleted(deletedPaths);
    kickNextDownload();
}

void ThetaCameraService::onBridgeDeleteError(const QString& errorMessage)
{
    if (m_deleteTimer) m_deleteTimer->stop();
    m_deleteActive = false;
    m_waitingForDeleteVerification = false;
    QStringList names;
    for (const CameraFileInfo& file : m_pendingDeleteFiles) {
        names << file.name;
    }
    LOGW("service") << "Delete failed for files:" << names
                    << "message:" << errorMessage;
    m_pendingDeleteFiles.clear();
    emit errorOccurred("Delete error: " + errorMessage);
    kickNextDownload();
}

void ThetaCameraService::onDeleteTimedOut()
{
    if (!m_deleteActive || m_pendingDeleteFiles.isEmpty()) {
        return;
    }

    QStringList names;
    for (const CameraFileInfo& file : m_pendingDeleteFiles) {
        names << file.name;
    }
    LOGW("service") << "Delete request timed out; refreshing catalog to verify result for:"
                    << names;
    m_waitingForDeleteVerification = true;
    m_bridge->refreshCatalog();
}
