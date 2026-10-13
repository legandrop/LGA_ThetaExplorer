#include "thetaexplorer/ThetaCameraWorker.h"
#include "thetaexplorer/CameraImageHeader.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QBuffer>
#include <QImageReader>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QQueue>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <limits>
#include <algorithm>
#include <memory>
#include <cstring>
#include <QDebug>
#include <QElapsedTimer>
#include <vector>
#include <chrono>
#include <utility>

QJsonObject ThetaCameraWorker::fileToJson(const CameraFileInfo& file)
{
    QJsonObject object{{"name", file.name}, {"path", file.devicePath}, {"uid", file.persistentId},
        {"uti", file.uti}, {"size", QString::number(file.sizeBytes)},
        {"date", file.creationDate.isValid() ? file.creationDate.toUTC().toString(Qt::ISODateWithMs) : QString()},
        {"width", file.width}, {"height", file.height}, {"raw", file.isRaw}, {"video", file.isVideo},
        {"mode", file.exposureMode}};
    if (std::isfinite(file.exposureValue)) object.insert("exposure", file.exposureValue);
    if (std::isfinite(file.exposureTime)) object.insert("seconds", file.exposureTime);
    return object;
}

bool ThetaCameraWorker::fileFromJson(const QJsonObject& object, CameraFileInfo& file)
{
    bool sizeOk = false;
    const qint64 size = object.value("size").toString().toLongLong(&sizeOk);
    if (!sizeOk || size < 0 || !object.value("name").isString() || !object.value("path").isString()
        || !object.value("uid").isString() || !object.value("date").isString()
        || !object.value("raw").isBool() || !object.value("video").isBool()) return false;
    CameraFileInfo parsed;
    parsed.name = object.value("name").toString();
    parsed.devicePath = object.value("path").toString();
    parsed.persistentId = object.value("uid").toString();
    if (parsed.name.isEmpty() || parsed.name.size() > 1024 || parsed.devicePath.isEmpty()
        || parsed.devicePath.size() > 16384 || parsed.persistentId.size() > 4096) return false;
    parsed.sizeBytes = size;
    parsed.uti = object.value("uti").toString();
    const QString date = object.value("date").toString();
    parsed.creationDate = QDateTime::fromString(date, Qt::ISODateWithMs);
    if (!date.isEmpty() && !parsed.creationDate.isValid()) return false;
    parsed.width = object.value("width").toInt();
    parsed.height = object.value("height").toInt();
    parsed.exposureMode = object.value("mode").toInt(-1);
    if (parsed.width < 0 || parsed.height < 0 || parsed.width > 100000 || parsed.height > 100000
        || parsed.exposureMode < -1 || parsed.exposureMode > 2) return false;
    parsed.isRaw = object.value("raw").toBool();
    parsed.isVideo = object.value("video").toBool();
    if (object.value("exposure").isDouble()) parsed.exposureValue = object.value("exposure").toDouble();
    if (object.value("seconds").isDouble()) parsed.exposureTime = object.value("seconds").toDouble();
    file = parsed;
    return true;
}

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <initguid.h>
#include <objbase.h>
#include <objidl.h>
#include <oleauto.h>
#include <cguid.h>
#include <portabledeviceapi.h>
#include <portabledevice.h>
#include <cfgmgr32.h>

namespace {
template<class T> class ComPtr {
public:
    ~ComPtr() { reset(); }
    T* get() const { return pointer; }
    T* operator->() const { return pointer; }
    T** out() { reset(); return &pointer; }
    void reset() { if (pointer) { pointer->Release(); pointer = nullptr; } }
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
private:
    T* pointer = nullptr;
};

template<class T> HRESULT createComObject(REFCLSID clsid, REFIID iid, ComPtr<T>& result)
{
    return CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, iid, reinterpret_cast<void**>(result.out()));
}

struct WorkerState {
    std::mutex mutex;
    std::condition_variable ready;
    QQueue<QJsonObject> jobs;
    QString epoch;
    QString activeId;
    std::atomic_bool stop{false};
    std::atomic_bool cancelled{false};
    std::atomic_bool changed{false};
    std::atomic<DWORD> gitCookie{0};
    std::atomic<DWORD> threadId{0};
};

QString hresultText(HRESULT result)
{
    return QString("0x%1").arg(quint32(result), 8, 16, QLatin1Char('0'));
}

QString nativeFilePath(const QString& path)
{
    const QString native = QDir::toNativeSeparators(path);
    if (native.startsWith(QStringLiteral("\\\\?\\"))) return native;
    if (native.startsWith(QStringLiteral("\\\\"))) return QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
    return QStringLiteral("\\\\?\\") + native;
}

QString stringValue(IPortableDeviceValues* values, REFPROPERTYKEY key)
{
    LPWSTR value = nullptr;
    if (FAILED(values->GetStringValue(key, &value))) return {};
    const QString text = value ? QString::fromWCharArray(value) : QString();
    CoTaskMemFree(value);
    return text;
}

bool safeName(const QString& name)
{
    if (name.isEmpty() || name == "." || name == ".." || name.endsWith('.') || name.endsWith(' ')) return false;
    for (const QChar c : name) {
        if (c.unicode() < 32 || QStringLiteral("/\\:*?\"<>|").contains(c)) return false;
    }
    const QString base = name.section('.', 0, 0).toUpper();
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL") return false;
    return !((base.startsWith("COM") || base.startsWith("LPT")) && base.size() == 4
        && base[3] >= QLatin1Char('1') && base[3] <= QLatin1Char('9'));
}

QImage decodeImage(const QByteArray& bytes)
{
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > 50000 || size.height() > 50000) return {};
    reader.setScaledSize(size.scaled(ThetaCameraWorker::MaxThumbnailDimension,
        ThetaCameraWorker::MaxThumbnailDimension, Qt::KeepAspectRatio));
    return reader.read();
}

QString encodedImage(const QImage& image)
{
    if (image.isNull()) return {};
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG") || bytes.size() > ThetaCameraWorker::MaxThumbnailBytes) return {};
    return QString::fromLatin1(bytes.toBase64());
}

class WpdWorker final : public QThread
{
    Q_OBJECT
public:
    explicit WpdWorker(std::shared_ptr<WorkerState> state) : state(std::move(state)) {}
signals:
    void packet(const QJsonObject& message);
protected:
    void run() override;
private:
    struct Entry {
        CameraFileInfo file;
        QString objectId;
        QString parentId;
        GUID contentType{};
        bool canDelete = true;
    };
    void reply(const QString& event, QJsonObject fields = {});
    void fail(const QString& message, HRESULT code = S_OK);
    bool interrupted() const { return state->stop || state->cancelled; }
    bool discover(bool refresh);
    bool openDevice(const QString& id);
    void closeDevice();
    bool catalog();
    bool properties(const QString& id, Entry& entry);
    bool validate(const CameraFileInfo& expected, Entry& entry, bool deleting);
    bool readResource(const Entry& entry, REFPROPERTYKEY key, int limit, QByteArray& bytes);
    void thumbnail(const CameraFileInfo& file);
    void imageInfo(const CameraFileInfo& file);
    void download(const CameraFileInfo& file, const QString& directory, const QString& staging);
    void removeFiles(const QJsonArray& files);
    void battery();
    std::shared_ptr<WorkerState> state;
    ComPtr<IPortableDeviceManager> manager;
    ComPtr<IPortableDevice> device;
    ComPtr<IPortableDeviceContent> content;
    ComPtr<IPortableDeviceProperties> props;
    ComPtr<IPortableDeviceResources> resources;
    ComPtr<IGlobalInterfaceTable> git;
    QHash<QString, Entry> entries;
    QString deviceId;
    QString deviceName;
    QString activeId;
    quint64 cameraEpoch = 0;
    bool readOnly = false;
};

void WpdWorker::reply(const QString& event, QJsonObject fields)
{
    fields.insert("v", 1);
    fields.insert("epoch", state->epoch);
    fields.insert("id", activeId);
    fields.insert("cameraEpoch", QString::number(cameraEpoch));
    fields.insert("event", event);
    emit packet(fields);
}

void WpdWorker::fail(const QString& message, HRESULT code)
{
    qWarning().noquote() << "Fallo del worker WPD:" << message << hresultText(code);
    reply("error", {{"message", message}, {"code", hresultText(code)}});
}

bool WpdWorker::properties(const QString& id, Entry& entry)
{
    ComPtr<IPortableDeviceKeyCollection> keys;
    if (FAILED(createComObject(CLSID_PortableDeviceKeyCollection, IID_IPortableDeviceKeyCollection, keys))) return false;
    const PROPERTYKEY requested[] = {WPD_OBJECT_NAME, WPD_OBJECT_ORIGINAL_FILE_NAME,
        WPD_OBJECT_PARENT_ID, WPD_OBJECT_CONTENT_TYPE, WPD_OBJECT_SIZE,
        WPD_OBJECT_DATE_CREATED, WPD_OBJECT_PERSISTENT_UNIQUE_ID, WPD_OBJECT_CAN_DELETE};
    for (const auto& key : requested) if (FAILED(keys->Add(key))) return false;
    ComPtr<IPortableDeviceValues> values;
    const HRESULT hr = props->GetValues(reinterpret_cast<LPCWSTR>(id.utf16()), keys.get(), values.out());
    if (FAILED(hr) || !values.get()) {
        qWarning().noquote() << "No se pudieron leer las propiedades WPD del objeto" << id << hresultText(hr);
        return false;
    }
    entry.objectId = id;
    entry.parentId = stringValue(values.get(), WPD_OBJECT_PARENT_ID);
    entry.file.name = stringValue(values.get(), WPD_OBJECT_ORIGINAL_FILE_NAME);
    if (entry.file.name.isEmpty()) entry.file.name = stringValue(values.get(), WPD_OBJECT_NAME);
    entry.file.persistentId = stringValue(values.get(), WPD_OBJECT_PERSISTENT_UNIQUE_ID);
    const HRESULT contentHr = values->GetGuidValue(WPD_OBJECT_CONTENT_TYPE, &entry.contentType);
    if (FAILED(contentHr)) {
        qWarning().noquote() << "El objeto WPD no tiene tipo de contenido" << id << hresultText(contentHr);
        return false;
    }
    ULONGLONG size = 0;
    if (SUCCEEDED(values->GetUnsignedLargeIntegerValue(WPD_OBJECT_SIZE, &size))
        && size <= ULONGLONG(std::numeric_limits<qint64>::max())) entry.file.sizeBytes = qint64(size);
    PROPVARIANT date{};
    if (SUCCEEDED(values->GetValue(WPD_OBJECT_DATE_CREATED, &date)) && date.vt == VT_DATE
        && std::isfinite(date.date) && date.date != 0) {
        SYSTEMTIME time{};
        if (VariantTimeToSystemTime(date.date, &time)) {
            const QDate day(time.wYear, time.wMonth, time.wDay);
            const QTime clock(time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
            if (day.isValid() && clock.isValid()) entry.file.creationDate = QDateTime(day, clock, Qt::UTC);
        }
    }
    PropVariantClear(&date);
    BOOL allowed = TRUE;
    if (SUCCEEDED(values->GetBoolValue(WPD_OBJECT_CAN_DELETE, &allowed))) entry.canDelete = allowed;
    const QString extension = QFileInfo(entry.file.name).suffix().toLower();
    entry.file.isRaw = extension == "dng";
    entry.file.isVideo = extension == "mp4" || extension == "mov";
    entry.file.uti = entry.file.isRaw ? "com.adobe.raw-image"
        : entry.file.isVideo ? "public.mpeg-4" : "public.jpeg";
    return true;
}

bool WpdWorker::validate(const CameraFileInfo& expected, Entry& entry, bool deleting)
{
    const auto found = entries.constFind(expected.devicePath);
    if (found == entries.cend() || expected.name != found->file.name
        || expected.sizeBytes != found->file.sizeBytes || expected.creationDate != found->file.creationDate
        || expected.persistentId != found->file.persistentId) {
        fail("The camera file changed. Refresh the camera files and try again.");
        return false;
    }
    Entry current;
    if (!properties(found->objectId, current) || current.parentId != found->parentId
        || current.file.name != expected.name || current.file.sizeBytes != expected.sizeBytes
        || current.file.creationDate != expected.creationDate
        || current.file.persistentId != expected.persistentId) {
        fail("The camera file is no longer the selected file. Refresh and try again.");
        return false;
    }
    if (deleting && (readOnly || !current.canDelete
        || (!expected.creationDate.isValid() && expected.persistentId.isEmpty()))) {
        fail("This camera file cannot be safely deleted.");
        return false;
    }
    current.file.devicePath = expected.devicePath;
    entry = current;
    return true;
}

bool WpdWorker::readResource(const Entry& entry, REFPROPERTYKEY key, int limit, QByteArray& bytes)
{
    ComPtr<IStream> stream;
    DWORD optimal = 65536;
    HRESULT hr = resources->GetStream(reinterpret_cast<LPCWSTR>(entry.objectId.utf16()), key,
        STGM_READ, &optimal, stream.out());
    if (FAILED(hr) || !stream.get()) return false;
    const int chunk = int(std::clamp<DWORD>(optimal, 4096, 1024 * 1024));
    QByteArray buffer(chunk, Qt::Uninitialized);
    while (bytes.size() < limit && !interrupted()) {
        ULONG received = 0;
        const ULONG requested = ULONG(std::min(chunk, limit - int(bytes.size())));
        hr = stream->Read(buffer.data(), requested, &received);
        if (FAILED(hr) || received > requested) return false;
        if (!received) break;
        bytes.append(buffer.constData(), received);
        if (hr == S_FALSE) break;
    }
    return !interrupted() && !bytes.isEmpty();
}

void WpdWorker::thumbnail(const CameraFileInfo& file)
{
    Entry entry;
    if (!validate(file, entry, false)) return;
    QByteArray bytes;
    QImage image;
    if (readResource(entry, WPD_RESOURCE_THUMBNAIL, ThetaCameraWorker::MaxThumbnailBytes, bytes)) image = decodeImage(bytes);
    if (image.isNull() && !file.isVideo && !interrupted()) {
        bytes.clear();
        if (readResource(entry, WPD_RESOURCE_DEFAULT, int(std::min<qint64>(file.sizeBytes,
            ThetaCameraWorker::MaxHeaderBytes)), bytes)) image = CameraImageHeader::parse(file, bytes).thumbnail;
    }
    const QString encoded = encodedImage(image);
    if (encoded.isEmpty()) reply("thumbnailFailed", {{"path", file.devicePath}});
    else reply("thumbnail", {{"path", file.devicePath}, {"image", encoded}});
}

void WpdWorker::imageInfo(const CameraFileInfo& file)
{
    Entry entry;
    if (!validate(file, entry, false)) return;
    QByteArray bytes;
    if (file.sizeBytes <= 0 || !readResource(entry, WPD_RESOURCE_DEFAULT,
        int(std::min<qint64>(file.sizeBytes, ThetaCameraWorker::MaxHeaderBytes)), bytes)) {
        fail("The image header could not be read.");
        return;
    }
    auto result = CameraImageHeader::parse(file, bytes);
    if (file.isRaw && result.headerValid && result.thumbnail.isNull()) {
        // El JPEG embebido puede quedar fuera del prefijo; WPD ofrece su propia miniatura.
        bytes.clear();
        if (readResource(entry, WPD_RESOURCE_THUMBNAIL, ThetaCameraWorker::MaxThumbnailBytes, bytes))
            result.thumbnail = decodeImage(bytes);
        if (!result.thumbnail.isNull()) result.error.clear();
    }
    if (!result.error.isEmpty()) { fail(result.error); return; }
    Entry verified;
    if (!validate(file, verified, false)) return;
    reply("imageInfo", {{"file", ThetaCameraWorker::fileToJson(result.file)}, {"image", encodedImage(result.thumbnail)}});
}

void WpdWorker::download(const CameraFileInfo& file, const QString& directory, const QString& staging)
{
    Entry entry;
    if (!validate(file, entry, false)) return;
    const QString destination = QFileInfo(directory).canonicalFilePath();
    const QFileInfo stage(staging);
    if (file.sizeBytes <= 0 || !safeName(file.name) || destination.isEmpty() || !stage.isDir()
        || stage.dir().canonicalPath() != destination || !stage.fileName().startsWith(".ThetaExplorer-transfer-")) {
        fail("The download destination is not valid."); return;
    }
    const QString stagedPath = QDir(stage.canonicalFilePath()).filePath(file.name);
    const QString finalPath = QDir(destination).filePath(file.name);
    QSaveFile output(stagedPath);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) { fail("The downloaded file could not be created."); return; }
    ComPtr<IStream> stream;
    DWORD optimal = 65536;
    HRESULT hr = resources->GetStream(reinterpret_cast<LPCWSTR>(entry.objectId.utf16()), WPD_RESOURCE_DEFAULT,
        STGM_READ, &optimal, stream.out());
    if (FAILED(hr) || !stream.get()) { fail("The camera file could not be opened.", hr); return; }
    const int chunk = int(std::clamp<DWORD>(optimal, 4096, 1024 * 1024));
    QByteArray buffer(chunk, Qt::Uninitialized);
    qint64 copied = 0;
    int lastPercent = -1;
    QElapsedTimer progressClock;
    progressClock.start();
    while (!interrupted()) {
        ULONG received = 0;
        hr = stream->Read(buffer.data(), ULONG(chunk), &received);
        if (FAILED(hr) || received > ULONG(chunk)) { fail("The camera transfer failed.", hr); return; }
        if (!received) break;
        if (qint64(received) > file.sizeBytes - copied || output.write(buffer.constData(), received) != received) {
            fail("The downloaded file does not match the expected size or could not be written."); return;
        }
        copied += received;
        const int percent = std::min(99, int(100.0 * double(copied) / double(file.sizeBytes)));
        if (percent != lastPercent || progressClock.elapsed() >= 1000) {
            reply("progress", {{"percent", percent}});
            lastPercent = percent;
            progressClock.restart();
        }
        if (hr == S_FALSE) break;
    }
    if (interrupted()) { fail("The camera transfer was cancelled. Refresh before trying again."); return; }
    if (copied != file.sizeBytes) { fail("The camera sent an incomplete file."); return; }
    Entry verified;
    if (!validate(file, verified, false)) return;
    if (!output.commit()) { fail("The downloaded file could not be saved."); return; }
    if (interrupted()) { fail("The camera transfer was cancelled before replacing the local file."); return; }
    // Qt admite rutas largas; el traslado Win32 necesita la misma forma extendida.
    const QString nativeStaged = nativeFilePath(stagedPath);
    const QString nativeFinal = nativeFilePath(finalPath);
    if (!MoveFileExW(reinterpret_cast<LPCWSTR>(nativeStaged.utf16()), reinterpret_cast<LPCWSTR>(nativeFinal.utf16()),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fail("The downloaded file could not replace the local copy.", HRESULT_FROM_WIN32(GetLastError())); return;
    }
    reply("progress", {{"percent", 100}});
    reply("download", {{"path", finalPath}});
}

void WpdWorker::removeFiles(const QJsonArray& files)
{
    if (files.isEmpty() || files.size() > 4096) { fail("The delete selection is not valid."); return; }
    QList<Entry> selection;
    QSet<QString> seen;
    for (const auto& value : files) {
        CameraFileInfo file;
        Entry entry;
        if (!value.isObject() || !ThetaCameraWorker::fileFromJson(value.toObject(), file)) {
            fail("The delete selection is not valid."); return;
        }
        if (seen.contains(file.devicePath)) { fail("The delete selection contains duplicate files."); return; }
        seen.insert(file.devicePath);
        if (!validate(file, entry, true)) return;
        if (IsEqualGUID(entry.contentType, WPD_CONTENT_TYPE_FOLDER)
            || IsEqualGUID(entry.contentType, WPD_CONTENT_TYPE_FUNCTIONAL_OBJECT)) {
            fail("Folders cannot be deleted by this application."); return;
        }
        selection.append(entry);
    }
    if (interrupted()) { fail("The delete request was cancelled before deleting any files."); return; }
    ComPtr<IPortableDevicePropVariantCollection> ids;
    HRESULT hr = createComObject(CLSID_PortableDevicePropVariantCollection, IID_IPortableDevicePropVariantCollection, ids);
    if (FAILED(hr)) { fail("The delete request could not be prepared.", hr); return; }
    for (const Entry& entry : selection) {
        PROPVARIANT id{};
        id.vt = VT_LPWSTR;
        const size_t bytes = size_t(entry.objectId.size() + 1) * sizeof(wchar_t);
        id.pwszVal = static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
        if (!id.pwszVal) { fail("The delete request could not be prepared.", E_OUTOFMEMORY); return; }
        std::memcpy(id.pwszVal, entry.objectId.utf16(), bytes);
        hr = ids->Add(&id);
        PropVariantClear(&id);
        if (FAILED(hr)) { fail("The delete request could not be prepared.", hr); return; }
    }
    ComPtr<IPortableDevicePropVariantCollection> results;
    hr = content->Delete(PORTABLE_DEVICE_DELETE_NO_RECURSION, ids.get(), results.out());
    if (FAILED(hr)) { fail("The camera could not delete the selected files.", hr); return; }
    DWORD count = 0;
    if (!results.get() || FAILED(results->GetCount(&count)) || count != DWORD(selection.size())) {
        fail("The delete result is uncertain. Refresh the camera files before trying again."); return;
    }
    QJsonArray removed, failed;
    for (DWORD i = 0; i < count; ++i) {
        PROPVARIANT result{};
        const HRESULT read = results->GetAt(i, &result);
        const Entry& entry = selection[int(i)];
        if (SUCCEEDED(read) && result.vt == VT_ERROR && result.scode == S_OK) {
            removed.append(entry.file.devicePath);
            entries.remove(entry.file.devicePath);
        } else {
            failed.append(entry.file.name);
        }
        PropVariantClear(&result);
    }
    reply("delete", {{"paths", removed}, {"failed", failed}});
}

void WpdWorker::battery()
{
    ComPtr<IPortableDeviceKeyCollection> keys;
    ComPtr<IPortableDeviceValues> values;
    DWORD level = 0;
    const bool available = SUCCEEDED(createComObject(CLSID_PortableDeviceKeyCollection, IID_IPortableDeviceKeyCollection, keys))
        && SUCCEEDED(keys->Add(WPD_DEVICE_POWER_LEVEL))
        && SUCCEEDED(props->GetValues(WPD_DEVICE_OBJECT_ID, keys.get(), values.out()))
        && values.get() && SUCCEEDED(values->GetUnsignedIntegerValue(WPD_DEVICE_POWER_LEVEL, &level)) && level <= 100;
    reply("battery", {{"available", available}, {"percent", available ? int(level) : 0}});
}

bool WpdWorker::catalog()
{
    struct Folder { QString id, path; int depth; };
    QQueue<Folder> folders;
    const QString root = "wpd/" + QString::fromLatin1(QCryptographicHash::hash(deviceId.toUtf8(),
        QCryptographicHash::Sha256).toHex());
    folders.enqueue({QString::fromWCharArray(WPD_DEVICE_OBJECT_ID), root, 0});
    QSet<QString> visited;
    QSet<QString> logicalPaths{root};
    QHash<QString, Entry> discovered;
    int unreadable = 0;
    while (!folders.isEmpty() && !interrupted()) {
        const Folder folder = folders.dequeue();
        if (folder.depth > 64 || visited.contains(folder.id) || visited.size() > 100000) {
            fail("The camera returned an invalid folder structure."); return false;
        }
        visited.insert(folder.id);
        ComPtr<IEnumPortableDeviceObjectIDs> iterator;
        HRESULT hr = content->EnumObjects(0, reinterpret_cast<LPCWSTR>(folder.id.utf16()), nullptr, iterator.out());
        if (FAILED(hr) || !iterator.get()) { fail("The camera folders could not be read.", hr); return false; }
        while (!interrupted()) {
            LPWSTR ids[32]{};
            DWORD count = 0;
            hr = iterator->Next(32, ids, &count);
            if (FAILED(hr) || count > 32) {
                for (auto* id : ids) CoTaskMemFree(id);
                fail("The camera file list could not be read.", hr); return false;
            }
            bool valid = true;
            for (DWORD i = 0; i < count; ++i) {
                Entry entry;
                const QString id = ids[i] ? QString::fromWCharArray(ids[i]) : QString();
                if (id.isEmpty()) { valid = false; break; }
                if (!properties(id, entry) || entry.file.name.isEmpty()
                    || entry.file.name.size() > 1024 || entry.file.name.contains(QChar(0))) {
                    qWarning().noquote() << "Entrada WPD incompleta:" << id << "nombre:" << entry.file.name;
                    ++unreadable;
                    continue;
                }
                entry.file.devicePath = folder.path + '/' + QString::fromLatin1(QUrl::toPercentEncoding(entry.file.name));
                // Una carpeta homónima también vuelve ambigua la identidad de sus archivos.
                if (logicalPaths.contains(entry.file.devicePath) || logicalPaths.size() >= 100000) {
                    qWarning().noquote() << "Ruta WPD duplicada o catálogo demasiado grande:" << entry.file.devicePath;
                    valid = false; break;
                }
                logicalPaths.insert(entry.file.devicePath);
                if (IsEqualGUID(entry.contentType, WPD_CONTENT_TYPE_FOLDER)
                    || IsEqualGUID(entry.contentType, WPD_CONTENT_TYPE_FUNCTIONAL_OBJECT)) {
                    folders.enqueue({id, entry.file.devicePath, folder.depth + 1});
                } else {
                    const QString ext = QFileInfo(entry.file.name).suffix().toLower();
                    if (!entry.file.isRaw && !entry.file.isVideo && ext != "jpg" && ext != "jpeg") continue;
                    if (discovered.contains(entry.file.devicePath) || discovered.size() >= 100000) { valid = false; break; }
                    discovered.insert(entry.file.devicePath, entry);
                }
            }
            for (auto* id : ids) CoTaskMemFree(id);
            if (!valid) { fail("The camera returned an ambiguous or incomplete file list."); return false; }
            if (!count || hr == S_FALSE) break;
        }
    }
    if (interrupted()) return false;
    if (unreadable && discovered.isEmpty()) {
        fail("The camera files could not be read. Refresh the camera and try again.");
        return false;
    }
    entries = discovered;
    QJsonArray files;
    for (const Entry& entry : std::as_const(entries)) files.append(ThetaCameraWorker::fileToJson(entry.file));
    reply("catalog", {{"files", files}, {"complete", unreadable == 0}});
    if (unreadable) fail(QString("%1 camera objects could not be read. The available files are shown; refresh to try again.").arg(unreadable));
    battery();
    return true;
}

bool WpdWorker::openDevice(const QString& id)
{
    ComPtr<IPortableDeviceValues> client;
    HRESULT hr = createComObject(CLSID_PortableDeviceValues, IID_IPortableDeviceValues, client);
    if (SUCCEEDED(hr)) hr = client->SetStringValue(WPD_CLIENT_NAME, L"Theta Explorer");
    if (SUCCEEDED(hr)) hr = client->SetUnsignedIntegerValue(WPD_CLIENT_SECURITY_QUALITY_OF_SERVICE, SECURITY_IMPERSONATION);
    if (SUCCEEDED(hr)) hr = client->SetUnsignedIntegerValue(WPD_CLIENT_DESIRED_ACCESS, GENERIC_READ | GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = createComObject(CLSID_PortableDeviceFTM, IID_IPortableDevice, device);
    if (SUCCEEDED(hr)) hr = device->Open(reinterpret_cast<LPCWSTR>(id.utf16()), client.get());
    readOnly = false;
    if (hr == E_ACCESSDENIED && device.get()) {
        client->SetUnsignedIntegerValue(WPD_CLIENT_DESIRED_ACCESS, GENERIC_READ);
        hr = device->Open(reinterpret_cast<LPCWSTR>(id.utf16()), client.get());
        readOnly = SUCCEEDED(hr);
    }
    if (SUCCEEDED(hr)) hr = device->Content(content.out());
    if (SUCCEEDED(hr)) hr = content->Properties(props.out());
    if (SUCCEEDED(hr)) hr = content->Transfer(resources.out());
    DWORD cookie = 0;
    if (SUCCEEDED(hr)) hr = git->RegisterInterfaceInGlobal(device.get(), IID_IPortableDevice, &cookie);
    if (FAILED(hr)) { fail("The camera could not be opened. Close other camera applications and try again.", hr); closeDevice(); return false; }
    state->gitCookie = cookie;
    deviceId = id;
    ++cameraEpoch;
    if (readOnly) qWarning() << "La cámara WPD se abrió sólo para lectura; el borrado está deshabilitado.";
    ComPtr<IPortableDeviceKeyCollection> identityKeys;
    ComPtr<IPortableDeviceValues> identityValues;
    QString serial;
    if (SUCCEEDED(createComObject(CLSID_PortableDeviceKeyCollection, IID_IPortableDeviceKeyCollection, identityKeys))
        && SUCCEEDED(identityKeys->Add(WPD_DEVICE_SERIAL_NUMBER))
        && SUCCEEDED(props->GetValues(WPD_DEVICE_OBJECT_ID, identityKeys.get(), identityValues.out()))
        && identityValues.get()) {
        serial = stringValue(identityValues.get(), WPD_DEVICE_SERIAL_NUMBER).trimmed().toUpper();
    }
    const QString identifier = serial.isEmpty() ? QString() : QStringLiteral("serial:") + serial;
    reply("connected", {{"name", deviceName}, {"cameraId", identifier}});
    return true;
}

void WpdWorker::closeDevice()
{
    entries.clear();
    const DWORD cookie = state->gitCookie.exchange(0);
    if (cookie && git.get()) git->RevokeInterfaceFromGlobal(cookie);
    resources.reset(); props.reset(); content.reset();
    if (device.get()) device->Close();
    device.reset();
    if (!deviceId.isEmpty()) {
        deviceId.clear();
        ++cameraEpoch;
        reply("disconnected");
    }
}

bool WpdWorker::discover(bool refresh)
{
    if (refresh && FAILED(manager->RefreshDeviceList())) { fail("The Windows camera list could not be refreshed."); return false; }
    DWORD count = 0;
    HRESULT hr = manager->GetDevices(nullptr, &count);
    if (FAILED(hr) || count > 4096) { fail("The Windows camera list could not be read.", hr); return false; }
    QList<LPWSTR> ids(int(count), nullptr);
    DWORD received = count;
    hr = manager->GetDevices(ids.data(), &received);
    QStringList available;
    if (SUCCEEDED(hr) && received <= count) {
        for (DWORD i = 0; i < received; ++i) if (ids[int(i)]) available.append(QString::fromWCharArray(ids[int(i)]));
    }
    for (auto* id : ids) CoTaskMemFree(id);
    if (FAILED(hr) || received > count) { fail("The Windows camera list could not be read.", hr); return false; }
    if (!deviceId.isEmpty() && available.contains(deviceId)) return true;
    closeDevice();
    std::sort(available.begin(), available.end());
    for (const QString& id : available) {
        auto description = [&](bool friendly) {
            DWORD length = 0;
            HRESULT code = friendly ? manager->GetDeviceFriendlyName(reinterpret_cast<LPCWSTR>(id.utf16()), nullptr, &length)
                : manager->GetDeviceDescription(reinterpret_cast<LPCWSTR>(id.utf16()), nullptr, &length);
            if (FAILED(code) || length == 0 || length > 32768) return QString();
            std::vector<wchar_t> text(length, 0);
            code = friendly ? manager->GetDeviceFriendlyName(reinterpret_cast<LPCWSTR>(id.utf16()), text.data(), &length)
                : manager->GetDeviceDescription(reinterpret_cast<LPCWSTR>(id.utf16()), text.data(), &length);
            return SUCCEEDED(code) ? QString::fromWCharArray(text.data()) : QString();
        };
        const QString name = description(true);
        if (!name.contains("THETA", Qt::CaseInsensitive) && !description(false).contains("THETA", Qt::CaseInsensitive)) continue;
        deviceName = name.isEmpty() ? QStringLiteral("RICOH THETA") : name;
        return openDevice(id) && catalog();
    }
    return true;
}

DWORD CALLBACK deviceChanged(HCMNOTIFICATION, PVOID context, CM_NOTIFY_ACTION,
    PCM_NOTIFY_EVENT_DATA, DWORD)
{
    auto* state = static_cast<WorkerState*>(context);
    state->changed = true;
    state->ready.notify_one();
    return ERROR_SUCCESS;
}

void WpdWorker::run()
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    activeId = "0";
    if (FAILED(initialized)) { fail("Windows camera support could not be initialized.", initialized); return; }
    CoEnableCallCancellation(nullptr);
    state->threadId = GetCurrentThreadId();
    const HRESULT managerHr = createComObject(CLSID_PortableDeviceManager, IID_IPortableDeviceManager, manager);
    const HRESULT gitHr = createComObject(CLSID_StdGlobalInterfaceTable, IID_IGlobalInterfaceTable, git);
    HCMNOTIFICATION notification = nullptr;
    CM_NOTIFY_FILTER filter{};
    filter.cbSize = sizeof(filter);
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    filter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_WPD;
    if (FAILED(managerHr) || FAILED(gitHr)) {
        fail("Windows camera support is not available.", FAILED(managerHr) ? managerHr : gitHr);
    } else {
        if (CM_Register_Notification(&filter, state.get(), deviceChanged, &notification) != CR_SUCCESS) {
            qWarning() << "No se pudo registrar la notificación USB; se usará la comprobación periódica.";
        }
        while (!state->stop) {
            QJsonObject job;
            {
                std::unique_lock<std::mutex> lock(state->mutex);
                state->ready.wait_for(lock, std::chrono::seconds(2), [&] { return state->stop || state->changed || !state->jobs.isEmpty(); });
                if (state->stop) break;
                if (!state->jobs.isEmpty()) job = state->jobs.dequeue();
                else job = {{"id", "0"}, {"op", "probe"}};
                activeId = job.value("id").toString();
                state->activeId = activeId;
                state->cancelled = false;
            }
            const QString op = job.value("op").toString();
            if (op == "browse" || op == "probe") {
                const bool changed = state->changed.exchange(false);
                discover(changed || !device.get() || !notification);
            } else if (op == "recover") {
                closeDevice(); discover(true);
            } else if (op == "refresh") {
                if (discover(true) && device.get()) catalog();
            } else if (!device.get()) {
                fail("The camera is no longer connected.");
            } else if (job.value("cameraEpoch").toString() != QString::number(cameraEpoch)) {
                fail("The camera session changed. Refresh the camera files and try again.");
            } else if (op == "delete") {
                removeFiles(job.value("files").toArray());
            } else {
                CameraFileInfo file;
                if (!ThetaCameraWorker::fileFromJson(job.value("file").toObject(), file)) fail("The camera request is not valid.");
                else if (op == "thumbnail") thumbnail(file);
                else if (op == "info") imageInfo(file);
                else if (op == "download") download(file, job.value("directory").toString(), job.value("staging").toString());
                else fail("The camera request is not supported.");
            }
            if (activeId != "0") reply("done");
            { std::lock_guard<std::mutex> lock(state->mutex); state->activeId.clear(); }
        }
    }
    if (notification) CM_Unregister_Notification(notification);
    closeDevice();
    manager.reset(); git.reset();
    state->threadId = 0;
    CoDisableCallCancellation(nullptr);
    CoUninitialize();
}

void cancelWorker(const std::shared_ptr<WorkerState>& state)
{
    state->cancelled = true;
    const DWORD threadId = state->threadId.load();
    if (threadId) CoCancelCall(threadId, 0);
    const DWORD cookie = state->gitCookie.load();
    if (!cookie) return;
    ComPtr<IGlobalInterfaceTable> git;
    ComPtr<IPortableDevice> device;
    if (SUCCEEDED(createComObject(CLSID_StdGlobalInterfaceTable, IID_IGlobalInterfaceTable, git))
        && SUCCEEDED(git->GetInterfaceFromGlobal(cookie, IID_IPortableDevice, reinterpret_cast<void**>(device.out())))) {
        device->Cancel();
    }
}
}

int ThetaCameraWorker::runThetaCameraWorker(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE
        || GetFileType(input) != FILE_TYPE_PIPE || GetFileType(output) != FILE_TYPE_PIPE) return 2;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) return 3;
    auto state = std::make_shared<WorkerState>();
    WpdWorker worker(state);
    QObject::connect(&worker, &WpdWorker::packet, &app, [&](const QJsonObject& packet) {
        QByteArray line = QJsonDocument(packet).toJson(QJsonDocument::Compact);
        if (line.size() > MaxLineBytes) { state->stop = true; state->ready.notify_one(); app.exit(4); return; }
        line.append('\n');
        DWORD written = 0;
        if (!WriteFile(output, line.constData(), DWORD(line.size()), &written, nullptr) || written != DWORD(line.size())) {
            state->stop = true; state->ready.notify_one(); app.exit(5);
        }
    }, Qt::QueuedConnection);
    QObject::connect(&worker, &QThread::finished, &app, &QCoreApplication::quit, Qt::QueuedConnection);
    QByteArray pending;
    QTimer reader;
    QElapsedTimer handshake;
    handshake.start();
    QObject::connect(&reader, &QTimer::timeout, &app, [&] {
        DWORD available = 0;
        if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr)) {
            state->stop = true; state->ready.notify_one(); cancelWorker(state); app.quit(); return;
        }
        if (state->epoch.isEmpty() && handshake.elapsed() > 10000) { app.exit(6); return; }
        if (!available) return;
        QByteArray bytes(int(std::min<DWORD>(available, 65536)), Qt::Uninitialized);
        DWORD received = 0;
        if (!ReadFile(input, bytes.data(), DWORD(bytes.size()), &received, nullptr)) { app.exit(5); return; }
        pending.append(bytes.constData(), received);
        if (pending.size() > MaxLineBytes && !pending.contains('\n')) { app.exit(4); return; }
        int newline = -1;
        while ((newline = pending.indexOf('\n')) >= 0) {
            if (newline > MaxLineBytes) { app.exit(4); return; }
            const QByteArray line = pending.left(newline);
            pending.remove(0, newline + 1);
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(line, &error);
            const auto job = document.object();
            const QString epoch = job.value("epoch").toString();
            const QString id = job.value("id").toString();
            const QString op = job.value("op").toString();
            bool idOk = false;
            const quint64 number = id.toULongLong(&idOk);
            if (error.error != QJsonParseError::NoError || !document.isObject() || job.value("v").toInt() != 1
                || epoch.isEmpty() || epoch.size() > 64 || !idOk || QString::number(number) != id) { app.exit(4); return; }
            if (state->epoch.isEmpty()) {
                if (op != "browse") { app.exit(4); return; }
                state->epoch = epoch;
                worker.start();
            } else if (epoch != state->epoch) { app.exit(4); return; }
            if (op == "stop" || op == "cancel") {
                bool current = false;
                { std::lock_guard<std::mutex> lock(state->mutex); current = state->activeId == id; }
                if (op == "stop") state->stop = true;
                if (current || op == "stop") cancelWorker(state);
                state->ready.notify_one();
            } else {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->jobs.size() >= 8) { app.exit(4); return; }
                state->jobs.enqueue(job);
                state->ready.notify_one();
            }
        }
    });
    reader.start(20);
    const int result = app.exec();
    state->stop = true;
    state->ready.notify_one();
    cancelWorker(state);
    if (!worker.wait(1500)) {
        // Sólo este proceso auxiliar: no destruir un QThread que todavía ejecuta COM.
        TerminateProcess(GetCurrentProcess(), 7);
        return 7;
    }
    CoUninitialize();
    return result;
}
#else
int ThetaCameraWorker::runThetaCameraWorker(int, char**) { return 2; }
#endif

#ifdef Q_OS_WIN
#include "ThetaCameraWorker.moc"
#endif
