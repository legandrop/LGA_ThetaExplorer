#include "thetaexplorer/DownloadHistory.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>

namespace {
constexpr qint64 kMaxHistoryBytes = 32 * 1024 * 1024;
QString hashText(const QString& value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QString identityKey(const QJsonArray& identity)
{
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact),
                                                      QCryptographicHash::Sha256).toHex());
}
}

DownloadHistory::DownloadHistory(const QString& path) : m_path(path)
{
    reload();
}

QString DownloadHistory::fileKey(const QString& cameraId, const CameraFileInfo& file)
{
    if (cameraId.trimmed().isEmpty() || file.name.isEmpty() || file.devicePath.isEmpty()
        || file.sizeBytes <= 0 || (!file.creationDate.isValid() && file.persistentId.isEmpty())) return {};
    const QJsonArray identity{hashText(cameraId), file.devicePath, file.name, QString::number(file.sizeBytes),
        file.creationDate.isValid() ? file.creationDate.toUTC().toString(Qt::ISODateWithMs) : QString(),
        file.persistentId};
    return identityKey(identity);
}

bool DownloadHistory::read(QJsonObject& entries, QString& error) const
{
    QFile source(m_path);
    if (!QFileInfo::exists(m_path)) { entries = {}; return true; }
    if (!source.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("No se pudo leer el historial: %1").arg(source.errorString()); return false;
    }
    if (source.size() > kMaxHistoryBytes) {
        error = QStringLiteral("El historial supera el limite de lectura"); return false;
    }
    const QByteArray data = source.read(kMaxHistoryBytes + 1);
    if (data.size() > kMaxHistoryBytes || !source.atEnd()) {
        error = QStringLiteral("El historial supera el limite de lectura"); return false;
    }
    if (source.error() != QFileDevice::NoError) {
        error = QStringLiteral("Lectura incompleta del historial: %1").arg(source.errorString()); return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value("version").toDouble(-1) != 1
        || !document.object().value("files").isObject()) {
        error = QStringLiteral("El historial tiene un formato invalido; se conserva sin reemplazar"); return false;
    }
    const QJsonObject parsed = document.object().value("files").toObject();
    static const QRegularExpression hashPattern(QStringLiteral("^[0-9a-f]{64}$"));
    for (auto it = parsed.begin(); it != parsed.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        const QString stamp = entry.value("downloadedAt").toString();
        const QDateTime date = QDateTime::fromString(stamp, Qt::ISODateWithMs);
        bool sizeOk = false;
        const qint64 size = entry.value("size").toString().toLongLong(&sizeOk);
        const QString captured = entry.value("capturedAt").toString();
        const QString uid = entry.value("uid").toString();
        const QDateTime capturedDate = QDateTime::fromString(captured, Qt::ISODateWithMs);
        const bool capturedValid = captured.isEmpty() ? !uid.isEmpty()
            : capturedDate.isValid() && capturedDate.toUTC().toString(Qt::ISODateWithMs) == captured;
        const QJsonArray identity{entry.value("camera").toString(), entry.value("path").toString(),
            entry.value("name").toString(), entry.value("size").toString(), captured, uid};
        if (!hashPattern.match(it.key()).hasMatch() || !it.value().isObject()
            || !date.isValid() || date.toUTC().toString(Qt::ISODateWithMs) != stamp
            || !hashPattern.match(entry.value("camera").toString()).hasMatch()
            || entry.value("name").toString().isEmpty() || entry.value("path").toString().isEmpty()
            || !sizeOk || size <= 0 || QString::number(size) != entry.value("size").toString()
            || !entry.value("capturedAt").isString() || !entry.value("uid").isString()
            || !capturedValid || identityKey(identity) != it.key()) {
            error = QStringLiteral("El historial contiene un registro invalido; se conserva sin reemplazar"); return false;
        }
    }
    entries = parsed;
    return true;
}

bool DownloadHistory::reload()
{
    QJsonObject loaded;
    QString error;
    if (!read(loaded, error)) { m_error = error; return false; }
    m_entries = loaded;
    m_error.clear();
    return true;
}

QDateTime DownloadHistory::lastDownloadedAt(const QString& cameraId, const CameraFileInfo& file) const
{
    const QString key = fileKey(cameraId, file);
    return key.isEmpty() ? QDateTime() : QDateTime::fromString(
        m_entries.value(key).toObject().value("downloadedAt").toString(), Qt::ISODateWithMs);
}

bool DownloadHistory::record(const QString& cameraId, const CameraFileInfo& file, const QDateTime& downloadedAt)
{
    const QString key = fileKey(cameraId, file);
    if (key.isEmpty() || !downloadedAt.isValid()) {
        m_error = QStringLiteral("La camara o el archivo no tienen identidad fiable para guardar el historial"); return false;
    }
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        m_error = QStringLiteral("No se pudo crear la carpeta del historial"); return false;
    }
    // Dos copias abiertas no deben pisar los registros de la otra al guardar el JSON.
    QLockFile lock(m_path + QStringLiteral(".lock"));
    if (!lock.tryLock(100)) {
        m_error = QStringLiteral("El historial esta ocupado por otra copia de la aplicacion"); return false;
    }
    QJsonObject updated;
    QString error;
    if (!read(updated, error)) { m_error = error; return false; }
    const QDateTime previous = QDateTime::fromString(updated.value(key).toObject()
        .value("downloadedAt").toString(), Qt::ISODateWithMs);
    if (!previous.isValid() || previous < downloadedAt) {
        updated.insert(key, QJsonObject{{"camera", hashText(cameraId)}, {"name", file.name},
            {"path", file.devicePath}, {"size", QString::number(file.sizeBytes)},
            {"capturedAt", file.creationDate.isValid() ? file.creationDate.toUTC().toString(Qt::ISODateWithMs) : QString()},
            {"uid", file.persistentId}, {"downloadedAt", downloadedAt.toUTC().toString(Qt::ISODateWithMs)}});
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 1}, {"files", updated}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > kMaxHistoryBytes) {
        m_error = QStringLiteral("El historial supera el limite de escritura"); return false;
    }
    QSaveFile destination(m_path);
    if (!destination.open(QIODevice::WriteOnly) || destination.write(bytes) != bytes.size() || !destination.commit()) {
        m_error = QStringLiteral("No se pudo guardar el historial: %1").arg(destination.errorString()); return false;
    }
    m_entries = updated;
    m_error.clear();
    return true;
}
