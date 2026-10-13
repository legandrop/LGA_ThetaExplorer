#include "thetaexplorer/CameraMediaCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <cmath>

namespace {
QString hash(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString imageHash(const QString& path, qint64 expectedSize)
{
    QFile source(path);
    QCryptographicHash checksum(QCryptographicHash::Sha256);
    if (!source.open(QIODevice::ReadOnly) || expectedSize <= 0 || source.size() != expectedSize) return {};
    qint64 remaining = expectedSize;
    while (remaining > 0) {
        const QByteArray chunk = source.read(qMin<qint64>(remaining, 64 * 1024));
        if (chunk.isEmpty()) return {};
        checksum.addData(chunk); remaining -= chunk.size();
    }
    if (!source.atEnd() || source.error() != QFileDevice::NoError) return {};
    return QString::fromLatin1(checksum.result().toHex());
}

bool safePath(const QString& root, const QString& path)
{
    if (root.isEmpty() || !QDir::isAbsolutePath(root)
        || (path != root && !path.startsWith(root + '/'))) return false;
    QString current = path;
    while (current.length() >= root.length()) {
        if (QFileInfo(current).isSymLink()) return false;
        if (current == root) return true;
        current = QFileInfo(current).absolutePath();
    }
    return false;
}

QJsonObject readJson(const QString& path)
{
    QFile input(path);
    if (QFileInfo(path).isSymLink() || !input.open(QIODevice::ReadOnly) || input.size() > 4096) return {};
    const QJsonDocument document = QJsonDocument::fromJson(input.readAll());
    if (input.error() != QFileDevice::NoError || !document.isObject()) return {};
    const QJsonObject object = document.object();
    return object.value("version").toInt(-1) == 1 ? object : QJsonObject{};
}
}

CameraMediaCache::CameraMediaCache(const QString& rootDirectory)
    : m_root(rootDirectory.isEmpty() ? QString() : QDir::cleanPath(rootDirectory))
{
}

QString CameraMediaCache::fileKey(const CameraFileInfo& file)
{
    if (file.name.isEmpty() || file.name.contains('/') || file.name.contains('\\')
        || file.name == "." || file.name == ".." || file.devicePath.isEmpty() || file.sizeBytes <= 0
        || (!file.creationDate.isValid() && file.persistentId.isEmpty())) return {};
    return hash(QJsonDocument(QJsonArray{file.devicePath, file.name, QString::number(file.sizeBytes),
        file.creationDate.isValid() ? file.creationDate.toUTC().toString(Qt::ISODateWithMs) : QString(),
        file.persistentId}).toJson(QJsonDocument::Compact));
}

QString CameraMediaCache::cameraDirectory(const QString& cameraId) const
{
    if (!cameraId.startsWith("serial:") || cameraId.mid(7).trimmed().isEmpty()) return {};
    const QString path = m_root + '/' + hash(cameraId.toUtf8());
    return safePath(m_root, path) ? path : QString();
}

QString CameraMediaCache::entryDirectory(const QString& cameraId, const CameraFileInfo& file) const
{
    const QString camera = cameraDirectory(cameraId), key = fileKey(file);
    if (camera.isEmpty() || key.isEmpty()) return {};
    const QString path = camera + '/' + key;
    return safePath(m_root, path) ? path : QString();
}

bool CameraMediaCache::ensureDirectory(const QString& directory)
{
    m_error.clear();
    if (directory.isEmpty() || !safePath(m_root, directory) || !QDir().mkpath(directory)
        || !safePath(m_root, directory)) {
        m_error = QStringLiteral("No se pudo crear un directorio seguro para la cache de imagenes");
        return false;
    }
    return true;
}

bool CameraMediaCache::writeJson(const QString& path, const QJsonObject& value)
{
    if (!safePath(m_root, path) || !ensureDirectory(QFileInfo(path).absolutePath())) return false;
    QSaveFile output(path);
    const QByteArray data = QJsonDocument(value).toJson(QJsonDocument::Compact);
    if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit()) {
        m_error = QStringLiteral("No se pudo guardar la cache: %1").arg(output.errorString());
        return false;
    }
    return true;
}

QPixmap CameraMediaCache::thumbnail(const QString& cameraId, const CameraFileInfo& file) const
{
    const QString directory = entryDirectory(cameraId, file);
    if (directory.isEmpty()) return {};
    const QString path = directory + "/thumb.png";
    if (!safePath(m_root, path)) return {};
    QImageReader reader(path);
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > 2048 || size.height() > 2048) return {};
    return QPixmap::fromImage(reader.read());
}

bool CameraMediaCache::storeThumbnail(const QString& cameraId, const CameraFileInfo& file, const QPixmap& image)
{
    if (image.isNull()) return true;
    const QString directory = entryDirectory(cameraId, file);
    if (!ensureDirectory(directory)) return false;
    const QString path = directory + "/thumb.png";
    if (!safePath(m_root, path)) return false;
    QSaveFile output(path);
    const QImage original = image.toImage();
    const QImage small = original.width() > 1024 || original.height() > 1024
        ? original.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation) : original;
    if (!output.open(QIODevice::WriteOnly) || !small.save(&output, "PNG") || !output.commit()) {
        m_error = QStringLiteral("No se pudo guardar la miniatura: %1").arg(output.errorString()); return false;
    }
    return true;
}

QString CameraMediaCache::preparePreviewDirectory(const QString& cameraId, const CameraFileInfo& file)
{
    const QString directory = entryDirectory(cameraId, file);
    if (directory.isEmpty()) return {};
    const QString destination = directory + "/preview";
    if (!ensureDirectory(destination)) return {};
    // Sin marca de commit, una copia interrumpida nunca se toma como preview completo.
    const QString marker = directory + "/preview.json";
    if (!safePath(m_root, marker) || (QFileInfo::exists(marker) && !QFile::remove(marker))) {
        m_error = QStringLiteral("No se pudo invalidar la copia anterior del preview"); return {};
    }
    return destination;
}

bool CameraMediaCache::commitPreview(const QString& cameraId, const CameraFileInfo& file, const QString& receivedPath)
{
    const QString directory = entryDirectory(cameraId, file);
    if (directory.isEmpty()) return false;
    const QString expected = directory + "/preview/" + file.name;
    const QFileInfo received(receivedPath), target(expected);
    if (!safePath(m_root, expected) || !safePath(m_root, received.absoluteFilePath())
        || target.canonicalFilePath().isEmpty() || target.canonicalFilePath() != received.canonicalFilePath()
        || received.fileName() != file.name || !received.isFile() || received.size() != file.sizeBytes
        || !QImageReader(expected).canRead()) {
        m_error = QStringLiteral("El preview recibido no coincide con la identidad del archivo"); return false;
    }
    QImageReader reader(expected);
    const QSize native = reader.size();
    if (!native.isValid() || native.width() > 100000 || native.height() > 100000) return false;
    reader.setScaledSize(native.scaled(320, 320, Qt::KeepAspectRatio));
    if (reader.read().isNull()) {
        m_error = QStringLiteral("No se pudo decodificar el preview recibido: %1").arg(reader.errorString()); return false;
    }
    const QString checksum = imageHash(expected, file.sizeBytes);
    if (checksum.isEmpty()) { m_error = QStringLiteral("No se pudo verificar el contenido del preview"); return false; }
    return writeJson(directory + "/preview.json", {{"version", 1}, {"size", QString::number(file.sizeBytes)}, {"sha256", checksum}});
}

QString CameraMediaCache::previewFile(const QString& cameraId, const CameraFileInfo& file) const
{
    const QString directory = entryDirectory(cameraId, file);
    if (directory.isEmpty()) return {};
    const QJsonObject marker = readJson(directory + "/preview.json");
    const QString checksum = marker.value("sha256").toString();
    if (marker.value("size").toString() != QString::number(file.sizeBytes) || checksum.length() != 64) return {};
    const QString path = directory + "/preview/" + file.name;
    const QFileInfo image(path);
    return safePath(m_root, path) && image.isFile() && image.size() == file.sizeBytes
        && imageHash(path, file.sizeBytes) == checksum ? path : QString();
}

void CameraMediaCache::enrichMetadata(const QString& cameraId, CameraFileInfo& file) const
{
    const QString directory = entryDirectory(cameraId, file);
    if (directory.isEmpty()) return;
    const QJsonObject data = readJson(directory + "/metadata.json");
    const int mode = data.value("exposureMode").toInt(-1);
    if (mode >= 0 && mode <= 2) file.exposureMode = mode;
    for (const auto& field : {QStringLiteral("exposureValue"), QStringLiteral("exposureTime")}) {
        if (!data.value(field).isDouble()) continue;
        const double value = data.value(field).toDouble();
        if (!std::isfinite(value) || (field == "exposureTime" && value <= 0)) continue;
        if (field == "exposureValue") file.exposureValue = value;
        else file.exposureTime = value;
    }
    const int width = data.value("width").toInt(), height = data.value("height").toInt();
    if (width > 0 && height > 0 && width <= 100000 && height <= 100000) {
        file.width = width; file.height = height;
    }
}

bool CameraMediaCache::storeMetadata(const QString& cameraId, const CameraFileInfo& file)
{
    const QString directory = entryDirectory(cameraId, file);
    if (directory.isEmpty()) return false;
    QJsonObject data{{"version", 1}, {"exposureMode", file.exposureMode}, {"width", file.width}, {"height", file.height}};
    if (std::isfinite(file.exposureValue)) data.insert("exposureValue", file.exposureValue);
    if (std::isfinite(file.exposureTime) && file.exposureTime > 0) data.insert("exposureTime", file.exposureTime);
    return writeJson(directory + "/metadata.json", data);
}

bool CameraMediaCache::removeEntry(const QString& directory)
{
    if (!safePath(m_root, directory) || directory == m_root) {
        m_error = QStringLiteral("La entrada de cache no es un directorio propio seguro"); return false;
    }
    if (!QFileInfo::exists(directory)) return true;
    if (!QDir(directory).removeRecursively()) {
        m_error = QStringLiteral("No se pudo retirar una entrada obsoleta de la cache"); return false;
    }
    return true;
}

bool CameraMediaCache::reconcile(const QString& cameraId, const QList<CameraFileInfo>& completeFiles)
{
    m_error.clear();
    const QString directory = cameraDirectory(cameraId);
    if (directory.isEmpty()) return false;
    QSet<QString> live;
    for (const CameraFileInfo& file : completeFiles) {
        const QString key = fileKey(file);
        if (!key.isEmpty()) live.insert(key);
    }
    static const QRegularExpression ownedName("^[0-9a-f]{64}$");
    bool ok = true;
    const QFileInfoList entries = QDir(directory).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        if (ownedName.match(entry.fileName()).hasMatch() && !live.contains(entry.fileName()))
            ok = removeEntry(entry.absoluteFilePath()) && ok;
    }
    return ok;
}

bool CameraMediaCache::removeFiles(const QString& cameraId, const QList<CameraFileInfo>& files)
{
    m_error.clear();
    bool ok = true;
    for (const CameraFileInfo& file : files) {
        const QString directory = entryDirectory(cameraId, file);
        if (!directory.isEmpty()) ok = removeEntry(directory) && ok;
    }
    return ok;
}
