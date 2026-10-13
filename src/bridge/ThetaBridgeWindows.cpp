#include "thetaexplorer/ThetaBridge.h"
#include "thetaexplorer/ThetaCameraWorker.h"
#include <QCoreApplication>
#include <QProcess>
#include <QTimer>
#include <QPointer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QDir>
#include <QBuffer>
#include <QImageReader>
#include <QUuid>
#include <QQueue>
#include <QSet>
#include <QDebug>
#include <memory>
#include <utility>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

class ThetaBridgePrivate final : public QObject
{
public:
    explicit ThetaBridgePrivate(ThetaBridge* bridge) : owner(bridge)
    {
        process = new QProcess(this);
        process->setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
        process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
            arguments->flags |= CREATE_NO_WINDOW;
        });
#endif
        deadline = new QTimer(this);
        deadline->setSingleShot(true);
        killTimer = new QTimer(this);
        killTimer->setSingleShot(true);
        connect(deadline, &QTimer::timeout, this, [this] {
            if (active.isEmpty() || stopping) return;
            const QString message = "The camera request timed out and its result is uncertain. Refresh before trying again.";
            qWarning() << "La operación USB venció; no se repetirá automáticamente.";
            reportError(message);
            stop();
            if (owner && connected) { connected = false; owner->cameraDisconnected(); }
        });
        connect(killTimer, &QTimer::timeout, this, [this] {
            if (process->state() != QProcess::NotRunning) process->kill();
        });
        connect(process, &QProcess::started, this, [this] { pump(); });
        connect(process, &QProcess::readyReadStandardOutput, this, [this] { receive(); });
        connect(process, &QProcess::readyReadStandardError, this, [this] {
            const QByteArray error = process->readAllStandardError().left(16384);
            if (!error.isEmpty()) qWarning().noquote() << "Worker de cámara:" << QString::fromUtf8(error).trimmed();
        });
        connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart) return;
            reportError("Windows camera support could not be started.");
            active = {}; queue.clear(); cleanupStage();
            if (detached) deleteLater();
        });
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int, QProcess::ExitStatus) {
            deadline->stop(); killTimer->stop();
            if (!stopping) {
                reportError("Windows camera support stopped unexpectedly. Refresh the camera files and try again.");
                if (owner && connected) { connected = false; owner->cameraDisconnected(); }
            }
            active = {}; queue.clear(); cleanupStage(); catalog.clear();
            if (detached) deleteLater();
        });
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] { stop(); });
    }

    void start()
    {
        if (detached || process->state() != QProcess::NotRunning) return;
        stopping = false;
        connected = false;
        sentHello = false;
        epoch = QUuid::createUuid().toString(QUuid::WithoutBraces);
        cameraEpoch = "0";
        buffer.clear(); queue.clear(); active = {}; catalog.clear(); cleanupStage();
        process->start(QCoreApplication::applicationFilePath(), {"--camera-worker"});
    }

    void stop()
    {
        if (stopping && process->state() == QProcess::NotRunning) return;
        stopping = true;
        deadline->stop(); queue.clear(); catalog.clear();
        if (process->state() != QProcess::NotRunning) {
            send({{"v", 1}, {"epoch", epoch}, {"id", "0"}, {"op", "stop"}});
            killTimer->start(2000);
            // Al salir de la app no habrá otro tick: pedir ya el cierre del helper propio.
            if (QCoreApplication::closingDown()) process->kill();
        } else {
            active = {}; cleanupStage();
        }
    }

    void detach()
    {
        owner.clear(); detached = true;
        stop();
        // Sólo al destruir el bridge: cerrar y limpiar aunque ya no haya ticks Qt.
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(1500);
        }
        if (process->state() == QProcess::NotRunning) {
            cleanupStage();
            deleteLater();
        } else {
            // No destruir un QProcess vivo ni borrar staging que el helper aún usa.
            qWarning().noquote() << "El auxiliar de cámara no terminó tras el cierre acotado; se retiene el controlador y su carpeta temporal:"
                << (stage ? stage->path() : QString());
        }
    }

    void refresh(bool recover)
    {
        if (process->state() == QProcess::NotRunning) { start(); return; }
        if (stopping) return;
        const QString op = recover ? "recover" : "refresh";
        for (const auto& pending : std::as_const(queue)) if (pending.value("op").toString() == op) return;
        queue.enqueue({{"op", op}});
        pump();
    }

    void request(const QString& op, const CameraFileInfo& file, const QString& directory = {}, quint64 requestId = 0)
    {
        QJsonObject job{{"op", op}, {"file", ThetaCameraWorker::fileToJson(file)},
            {"directory", directory}, {"requestId", QString::number(requestId)}};
        if (!current(file) || stopping || !connected) {
            reportError("The camera file is no longer available. Refresh and try again.", job); return;
        }
        if (op == "thumbnail") {
            for (const auto& queued : std::as_const(queue)) {
                if (queued.value("op").toString() == op
                    && queued.value("file").toObject().value("path").toString() == file.devicePath) return;
            }
            if (queue.size() >= 64) { reportError("The thumbnail queue is full.", job); return; }
        }
        if (queue.size() >= 128) { reportError("The camera request queue is full.", job); return; }
        queue.enqueue(job);
        pump();
    }

    void remove(const QList<CameraFileInfo>& files)
    {
        QJsonArray encoded;
        QSet<QString> seen;
        for (const auto& file : files) {
            if (!current(file) || seen.contains(file.devicePath)) {
                if (owner) owner->deleteError("The delete selection changed. Refresh and try again."); return;
            }
            seen.insert(file.devicePath);
            encoded.append(ThetaCameraWorker::fileToJson(file));
        }
        if (stopping || !connected || encoded.isEmpty() || encoded.size() > 4096 || queue.size() >= 128) {
            if (owner) owner->deleteError("The delete request is not available."); return;
        }
        queue.enqueue({{"op", "delete"}, {"files", encoded}});
        pump();
    }

private:
    void cleanupStage()
    {
        if (stage && stage->isValid() && !stage->remove()) {
            qWarning().noquote() << "No se pudo limpiar la carpeta temporal propia de la descarga:" << stage->path();
            stage->setAutoRemove(false);
        }
        stage.reset();
    }
    bool current(const CameraFileInfo& file) const
    {
        const auto found = catalog.constFind(file.devicePath);
        return found != catalog.cend() && found->name == file.name && found->sizeBytes == file.sizeBytes
            && found->creationDate == file.creationDate && found->persistentId == file.persistentId;
    }
    bool send(const QJsonObject& object)
    {
        QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact);
        if (line.size() > ThetaCameraWorker::MaxLineBytes) return false;
        line.append('\n');
        return process->write(line) == line.size();
    }
    void reportError(const QString& message, const QJsonObject& job = {})
    {
        if (!owner || detached) return;
        const QJsonObject request = job.isEmpty() ? active : job;
        const QString op = request.value("op").toString();
        const auto file = request.value("file").toObject();
        if (op == "thumbnail") owner->thumbnailFailed(file.value("path").toString());
        else if (op == "info") owner->imageInfoFailed(file.value("path").toString(), message,
            request.value("requestId").toString().toULongLong());
        else if (op == "download") owner->downloadError(file.value("name").toString(), message);
        else if (op == "delete") owner->deleteError(message);
        else owner->cameraError(message);
    }
    void protocolError()
    {
        reportError("Windows camera support returned an invalid response. Refresh and try again.");
        stop();
        if (owner && connected) { connected = false; owner->cameraDisconnected(); }
        if (process->state() != QProcess::NotRunning) process->kill();
    }
    void pump()
    {
        if (stopping || detached || process->state() != QProcess::Running || !active.isEmpty()) return;
        if (!sentHello) {
            active = {{"op", "browse"}};
            sentHello = true;
        } else {
            if (queue.isEmpty()) return;
            int best = 0;
            auto priority = [](const QJsonObject& job) {
                const QString op = job.value("op").toString();
                return op == "delete" || op == "download" ? 0 : op == "info" ? 1 : op == "thumbnail" ? 3 : 2;
            };
            for (int i = 1; i < queue.size(); ++i) if (priority(queue[i]) < priority(queue[best])) best = i;
            active = queue.takeAt(best);
        }
        active.insert("v", 1);
        active.insert("epoch", epoch);
        active.insert("id", QString::number(++nextId));
        active.insert("cameraEpoch", cameraEpoch);
        responseSeen = false;
        failed = false;
        const QString op = active.value("op").toString();
        if (op == "download") {
            const QString directory = active.value("directory").toString();
            if (!QDir::isAbsolutePath(directory) || !QFileInfo(directory).isDir()) {
                reportError("The download destination is not valid."); active = {}; pump(); return;
            }
            stage = std::make_unique<QTemporaryDir>(QDir(directory).filePath(".ThetaExplorer-transfer-XXXXXX"));
            if (!stage->isValid()) {
                reportError("The download staging folder could not be created."); cleanupStage(); active = {}; pump(); return;
            }
            active.insert("staging", stage->path());
        }
        if (!send(active)) { protocolError(); return; }
        deadline->start(op == "download" ? 30000 : op == "info" ? 12000 : op == "thumbnail" ? 8000 : op == "delete" ? 7000 : 60000);
    }
    QPixmap image(const QJsonObject& packet, bool& valid)
    {
        const QString text = packet.value("image").toString();
        if (text.isEmpty()) return {};
        if (text.size() > (ThetaCameraWorker::MaxThumbnailBytes + 2) / 3 * 4) { valid = false; return {}; }
        const auto decoded = QByteArray::fromBase64Encoding(text.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) { valid = false; return {}; }
        QBuffer source;
        source.setData(decoded.decoded);
        source.open(QIODevice::ReadOnly);
        QImageReader reader(&source);
        const QSize size = reader.size();
        if (!size.isValid() || size.width() > ThetaCameraWorker::MaxThumbnailDimension
            || size.height() > ThetaCameraWorker::MaxThumbnailDimension) { valid = false; return {}; }
        const QImage result = reader.read();
        if (result.isNull()) valid = false;
        return QPixmap::fromImage(result);
    }
    void receive()
    {
        const QPointer<ThetaBridgePrivate> alive(this);
        buffer.append(process->readAllStandardOutput());
        int newline = -1;
        while ((newline = buffer.indexOf('\n')) >= 0) {
            if (newline > ThetaCameraWorker::MaxLineBytes) { protocolError(); return; }
            const QByteArray line = buffer.left(newline);
            buffer.remove(0, newline + 1);
            if (stopping || !owner) continue;
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(line, &error);
            const QJsonObject packet = document.object();
            if (error.error != QJsonParseError::NoError || !document.isObject()
                || packet.value("v").toInt() != 1 || !packet.value("id").isString()
                || !packet.value("event").isString() || !packet.value("epoch").isString()
                || packet.value("epoch").toString().isEmpty()) { protocolError(); return; }
            if (packet.value("epoch").toString() != epoch) continue;
            const QString id = packet.value("id").toString();
            bool idOk = false;
            const quint64 number = id.toULongLong(&idOk);
            if (!idOk || QString::number(number) != id || number > nextId) { protocolError(); return; }
            if (id != "0" && (active.isEmpty() || id != active.value("id").toString())) continue;
            if (!consume(packet)) { protocolError(); return; }
            if (!alive) return;
        }
        if (buffer.size() > ThetaCameraWorker::MaxLineBytes) protocolError();
    }
    bool consume(const QJsonObject& packet)
    {
        const QString event = packet.value("event").toString();
        const QString id = packet.value("id").toString();
        const QString op = active.value("op").toString();
        if (event == "connected" || event == "disconnected" || event == "catalog" || event == "battery") {
            bool epochOk = false;
            const quint64 revision = packet.value("cameraEpoch").toString().toULongLong(&epochOk);
            if (!epochOk) return false;
            if (revision < cameraEpoch.toULongLong()) return true;
            cameraEpoch = QString::number(revision);
        }
        if (event == "connected") {
            const QString name = packet.value("name").toString();
            if (name.isEmpty() || (packet.contains("cameraId") && !packet.value("cameraId").isString())) return false;
            connected = true;
            owner->cameraIdentityReady(packet.value("cameraId").toString());
            if (owner) owner->cameraConnected(name);
        } else if (event == "disconnected") {
            connected = false; catalog.clear(); queue.clear();
            owner->cameraDisconnected();
        } else if (event == "catalog") {
            if (!packet.value("files").isArray()) return false;
            const auto array = packet.value("files").toArray();
            if (array.size() > 100000) return false;
            QList<CameraFileInfo> files;
            QHash<QString, CameraFileInfo> updated;
            for (const auto& value : array) {
                CameraFileInfo file;
                if (!value.isObject() || !ThetaCameraWorker::fileFromJson(value.toObject(), file)
                    || updated.contains(file.devicePath)) return false;
                files.append(file); updated.insert(file.devicePath, file);
            }
            catalog = updated;
            owner->fileListUpdated(files);
        } else if (event == "battery") {
            if (!packet.value("available").isBool() || !packet.value("percent").isDouble()) return false;
            const int percent = packet.value("percent").toInt(-1);
            if (percent < 0 || percent > 100) return false;
            owner->batteryLevelChanged(percent, packet.value("available").toBool());
        } else if (event == "error") {
            const QString message = packet.value("message").toString();
            if (message.isEmpty()) return false;
            if (id == "0") owner->cameraError(message);
            else { failed = true; responseSeen = true; reportError(message); }
        } else if (event == "done") {
            if (id == "0" || active.isEmpty()) return false;
            if (!responseSeen && (op == "thumbnail" || op == "info" || op == "download" || op == "delete")) return false;
            deadline->stop(); cleanupStage(); active = {};
            pump();
        } else if (event == "progress") {
            if (id == "0" || op != "download" || !packet.value("percent").isDouble()) return false;
            const int percent = packet.value("percent").toInt(-1);
            if (percent < 0 || percent > 100) return false;
            deadline->start(30000);
            owner->downloadProgress(active.value("file").toObject().value("name").toString(), percent);
        } else if (event == "thumbnail" || event == "thumbnailFailed") {
            if (id == "0" || op != "thumbnail" || responseSeen) return false;
            const QString path = active.value("file").toObject().value("path").toString();
            if (packet.value("path").toString() != path) return false;
            responseSeen = true;
            if (event == "thumbnailFailed") owner->thumbnailFailed(path);
            else {
                bool valid = true;
                const QPixmap pixmap = image(packet, valid);
                if (!valid || pixmap.isNull()) return false;
                owner->thumbnailReady(path, pixmap);
            }
        } else if (event == "imageInfo") {
            if (id == "0" || op != "info" || responseSeen) return false;
            CameraFileInfo file;
            if (!ThetaCameraWorker::fileFromJson(packet.value("file").toObject(), file) || !current(file)
                || file.devicePath != active.value("file").toObject().value("path").toString()) return false;
            bool valid = true;
            const QPixmap pixmap = image(packet, valid);
            if (!valid || (file.isRaw && pixmap.isNull())) return false;
            responseSeen = true;
            owner->imageInfoReady(file, pixmap, active.value("requestId").toString().toULongLong());
        } else if (event == "download") {
            if (id == "0" || op != "download" || responseSeen || failed) return false;
            const auto file = active.value("file").toObject();
            bool sizeOk = false;
            const qint64 expectedSize = file.value("size").toString().toLongLong(&sizeOk);
            const QString expected = QDir(active.value("directory").toString()).filePath(file.value("name").toString());
            const QFileInfo received(packet.value("path").toString());
            if (!sizeOk || expectedSize <= 0 || received.canonicalFilePath().isEmpty()
                || received.canonicalFilePath() != QFileInfo(expected).canonicalFilePath()
                || received.fileName() != file.value("name").toString() || received.size() != expectedSize) return false;
            responseSeen = true;
            owner->downloadFileCompleted(file.value("name").toString(), received.canonicalFilePath());
        } else if (event == "delete") {
            if (id == "0" || op != "delete" || responseSeen || failed
                || !packet.value("paths").isArray() || !packet.value("failed").isArray()) return false;
            QHash<QString, QString> expected;
            for (const auto& value : active.value("files").toArray()) {
                const auto file = value.toObject();
                expected.insert(file.value("path").toString(), file.value("name").toString());
            }
            QStringList removed, errors;
            for (const auto& value : packet.value("paths").toArray()) {
                const QString path = value.toString();
                if (!value.isString() || !expected.remove(path)) return false;
                removed.append(path);
            }
            QStringList failedNames = expected.values();
            for (const auto& value : packet.value("failed").toArray()) {
                if (!value.isString() || !failedNames.removeOne(value.toString())) return false;
                errors.append(value.toString());
            }
            if (removed.size() + errors.size() != active.value("files").toArray().size()) return false;
            for (const QString& path : removed) catalog.remove(path);
            responseSeen = true;
            if (!removed.isEmpty()) owner->deleteCompleted(removed);
            if (!errors.isEmpty() && owner) owner->deleteError("Failed to delete: " + errors.join(", "));
        } else return false;
        return true;
    }
    QPointer<ThetaBridge> owner;
    QProcess* process = nullptr;
    QTimer* deadline = nullptr;
    QTimer* killTimer = nullptr;
    QString epoch, cameraEpoch;
    quint64 nextId = 0;
    QQueue<QJsonObject> queue;
    QJsonObject active;
    QHash<QString, CameraFileInfo> catalog;
    QByteArray buffer;
    std::unique_ptr<QTemporaryDir> stage;
    bool stopping = true, detached = false, connected = false, sentHello = false;
    bool responseSeen = false, failed = false;
};

ThetaBridge::ThetaBridge(QObject* parent) : QObject(parent), d(new ThetaBridgePrivate(this)) {}
ThetaBridge::~ThetaBridge() { d->detach(); d = nullptr; }
void ThetaBridge::startBrowsing() { d->start(); }
void ThetaBridge::stopBrowsing() { d->stop(); }
void ThetaBridge::refreshCatalog() { d->refresh(false); }
void ThetaBridge::recoverDownloadSession() { d->refresh(true); }
void ThetaBridge::requestThumbnail(const CameraFileInfo& file) { d->request("thumbnail", file); }
void ThetaBridge::requestImageInfo(const CameraFileInfo& file, quint64 requestId) { d->request("info", file, {}, requestId); }
void ThetaBridge::requestDownloadFile(const CameraFileInfo& file, const QString& directory) { d->request("download", file, directory); }
void ThetaBridge::requestDownloadFiles(const QList<CameraFileInfo>& files, const QString& directory)
{ for (const auto& file : files) requestDownloadFile(file, directory); }
void ThetaBridge::requestDeleteFiles(const QList<CameraFileInfo>& files) { d->remove(files); }
