// ThetaBridge.mm
// Objective-C++ implementation of the Qt <-> ImageCaptureCore bridge.
// This is the ONLY file that imports Objective-C/ImageCaptureCore headers.
//
// KEY DESIGN NOTE — debounce pattern:
// ImageCaptureCore fires many delegate callbacks in rapid succession during
// camera init (didAddItems, cameraDeviceDidChangeCapability, didReceiveSessionOptions,
// deviceDidBecomeReadyWithCompleteContentCatalog, ...).  If we call enumerateFiles
// directly from each one we get a burst of fileListUpdated signals that causes the
// Qt grid to be rebuilt 5-6 times, the thumbnail timer to be restarted each time,
// and ultimately a PTP command-queue flood that hangs the camera.
//
// Solution: scheduleEnumeration() cancels any pending GCD block and posts a new one
// 300 ms into the future.  The burst collapses into a single enumeration.

#import "ThetaBridgePrivate.h"
#include "thetaexplorer/Logger.h"
#include <QMetaObject>
#include <QPixmap>
#include <QImage>
#include <QPointer>
#include <QtEndian>
#include <CoreGraphics/CoreGraphics.h>
#include <vector>
#include <cmath>
#include <algorithm>
#import <ImageIO/ImageIO.h>

// ============================================================
// ThetaBridgePrivate: holds all Objective-C objects
// ============================================================
class ThetaBridgePrivate {
public:
    ICDeviceBrowser*            browser    = nil;
    ThetaDeviceBrowserAdapter*  browserDel = nil;
    ThetaCameraDeviceAdapter*   cameraDel  = nil;
};

// ============================================================
// ThetaBridge (QObject) - method implementations
// ============================================================
ThetaBridge::ThetaBridge(QObject* parent)
    : QObject(parent)
    , d(new ThetaBridgePrivate)
{}

ThetaBridge::~ThetaBridge()
{
    stopBrowsing();
    delete d;
}

void ThetaBridge::startBrowsing()
{
    d->browserDel           = [[ThetaDeviceBrowserAdapter alloc] init];
    d->browserDel.qtBridge  = this;

    d->browser              = [[ICDeviceBrowser alloc] init];
    d->browser.delegate     = d->browserDel;
    // Must cast because browsedDeviceTypeMask is ICDeviceTypeMask (NS_ENUM NSUInteger)
    // and the OR of two NS_ENUMs produces plain NSUInteger
    d->browser.browsedDeviceTypeMask =
        (ICDeviceTypeMask)(ICDeviceTypeMaskCamera | ICDeviceLocationTypeMaskLocal);
    [d->browser start];
    LOGI("bridge") << "startBrowsing";
}

void ThetaBridge::stopBrowsing()
{
    if (d->browser) {
        [d->browser stop];
        d->browser.delegate = nil;
        d->browser = nil;
    }
}

void ThetaBridge::refreshCatalog()
{
    if (d->cameraDel && d->cameraDel.camera) {
        LOGI("bridge") << "refreshCatalog on active session";
        [d->cameraDel scheduleEnumeration];
        return;
    }

    LOGI("bridge") << "refreshCatalog without active session; restarting browser";
    stopBrowsing();
    startBrowsing();
}

void ThetaBridge::recoverDownloadSession()
{
    if (d->cameraDel && d->cameraDel.camera) {
        LOGW("bridge") << "recoverDownloadSession requested";
        [d->cameraDel recoverSessionForDownloads];
        return;
    }

    LOGW("bridge") << "recoverDownloadSession without active camera; restarting browser";
    stopBrowsing();
    startBrowsing();
}

void ThetaBridge::requestThumbnail(const CameraFileInfo& file)
{
    if (!d->cameraDel || !d->cameraDel.camera) return;
    ICCameraFile* icFile = (__bridge ICCameraFile*)file.nativeFileHandle;
    if (icFile) [icFile requestThumbnail];
}

void ThetaBridge::requestDownloadFile(const CameraFileInfo& file,
                                      const QString& destinationPath)
{
    if (!d->cameraDel || !d->cameraDel.camera) {
        LOGW("bridge") << "requestDownloadFile: no camera";
        emit downloadError(file.name, "The camera is no longer connected.");
        return;
    }

    ICCameraFile* icFile = (__bridge ICCameraFile*)file.nativeFileHandle;
    if (!icFile) {
        LOGW("bridge") << "requestDownloadFile: invalid file handle for" << file.name;
        emit downloadError(file.name, "The camera file is no longer available. Refresh the camera files and try again.");
        return;
    }

    ICCameraDevice* cam = d->cameraDel.camera;
    NSURL* destURL = [NSURL fileURLWithPath:destinationPath.toNSString()];
    NSDictionary* options = @{
        ICDownloadsDirectoryURL:         destURL,
        ICOverwrite:                     @YES,
        ICDeleteAfterSuccessfulDownload: @NO
    };
    [cam requestDownloadFile:icFile
                     options:options
            downloadDelegate:d->cameraDel
         didDownloadSelector:@selector(didDownloadFile:error:options:contextInfo:)
                 contextInfo:NULL];
    LOGD("bridge") << "Queued download:"
                   << file.name
                   << "destDir:" << destinationPath
                   << "sizeBytes:" << file.sizeBytes
                   << "devicePath:" << file.devicePath;
}

void ThetaBridge::requestDownloadFiles(const QList<CameraFileInfo>& files,
                                        const QString& destinationPath)
{
    for (const CameraFileInfo& fi : files) {
        requestDownloadFile(fi, destinationPath);
    }
}

void ThetaBridge::requestDeleteFiles(const QList<CameraFileInfo>& files)
{
    if (!d->cameraDel || !d->cameraDel.camera) {
        LOGW("bridge") << "requestDeleteFiles: no camera";
        return;
    }

    ICCameraDevice* camera = d->cameraDel.camera;
    NSMutableArray<ICCameraItem*>* icFiles = [NSMutableArray array];
    QStringList fileNames;
    QStringList filePaths;
    for (const CameraFileInfo& fi : files) {
        ICCameraFile* icFile = (__bridge ICCameraFile*)fi.nativeFileHandle;
        if (icFile) {
            [icFiles addObject:icFile];
            fileNames << fi.name;
            filePaths << fi.devicePath;
        }
    }
    if (icFiles.count == 0) {
        LOGW("bridge") << "requestDeleteFiles: no valid file handles";
        return;
    }

    const bool canDeleteOne = [camera.capabilities containsObject:ICCameraDeviceCanDeleteOneFile];
    const bool canDeleteAll = [camera.capabilities containsObject:ICCameraDeviceCanDeleteAllFiles];
    LOGI("bridge") << "requestDeleteFiles:" << (int)icFiles.count
                   << "canDeleteOne:" << canDeleteOne
                   << "canDeleteAll:" << canDeleteAll
                   << "files:" << fileNames
                   << "devicePaths:" << filePaths;

    if (@available(macOS 10.15, *)) {
        d->cameraDel.usingBlockDeleteAPI = YES;
        ThetaBridge* weakBridge = this;
        __weak ThetaCameraDeviceAdapter* weakDelegate = d->cameraDel;
        __block QString deleteFailureSummary;
        d->cameraDel.activeDeleteProgress =
            [camera requestDeleteFiles:icFiles
                          deleteFailed:^(NSDictionary<ICDeleteError,ICCameraItem*>* failures) {
                ThetaBridge* strongBridge = weakBridge;
                if (!strongBridge) {
                    return;
                }

                QStringList failureDetails;
                QStringList failureMessages;
                for (NSString* key in failures) {
                    ICCameraItem* item = failures[key];
                    const QString itemName = item.name ? QString::fromNSString(item.name) : QString();
                    const QString reason = key ? QString::fromNSString(key) : QString("UnknownDeleteError");
                    failureDetails << QString("%1 (%2)").arg(itemName, reason);

                    QString prettyReason = reason;
                    if ([key isEqualToString:ICDeleteErrorReadOnly]) {
                        prettyReason = "read-only on camera";
                    } else if ([key isEqualToString:ICDeleteErrorFileMissing]) {
                        prettyReason = "missing on camera";
                    } else if ([key isEqualToString:ICDeleteErrorDeviceMissing]) {
                        prettyReason = "camera disconnected";
                    } else if ([key isEqualToString:ICDeleteErrorCanceled]) {
                        prettyReason = "delete canceled";
                    }
                    failureMessages << QString("%1: %2").arg(itemName, prettyReason);
                }

                deleteFailureSummary = failureMessages.join("; ");
                LOGW("bridge") << "Delete failed for individual items:" << failureDetails;
            }
                           completion:^(NSDictionary<ICDeleteResult,NSArray<ICCameraItem*>*>* result, NSError* _Nullable error) {
                ThetaBridge* strongBridge = weakBridge;
                ThetaCameraDeviceAdapter* strongDelegate = weakDelegate;
                if (!strongBridge || !strongDelegate) {
                    return;
                }

                strongDelegate.activeDeleteProgress = nil;
                strongDelegate.usingBlockDeleteAPI = NO;

                NSArray<ICCameraItem*>* successItems = result[ICDeleteSuccessful];
                NSArray<ICCameraItem*>* failedItems = result[ICDeleteFailed];
                NSArray<ICCameraItem*>* canceledItems = result[ICDeleteCanceled];

                QStringList successPaths;
                for (ICCameraItem* item in successItems ?: @[]) {
                    NSString* p = item.fileSystemPath;
                    successPaths << (p ? QString::fromNSString(p) : QString::fromNSString(item.name ?: @""));
                }

                QStringList failedNames;
                for (ICCameraItem* item in failedItems ?: @[]) {
                    failedNames << QString::fromNSString(item.name ?: @"");
                }

                QStringList canceledNames;
                for (ICCameraItem* item in canceledItems ?: @[]) {
                    canceledNames << QString::fromNSString(item.name ?: @"");
                }

                LOGI("bridge") << "Delete completion"
                               << "success:" << successPaths
                               << "failed:" << failedNames
                               << "canceled:" << canceledNames
                               << "errorCode:" << (error ? error.code : 0)
                               << "errorDomain:" << (error ? QString::fromNSString(error.domain ?: @"") : QString());

                if (!successPaths.isEmpty()) {
                    QMetaObject::invokeMethod(strongBridge, "deleteCompleted",
                                              Qt::QueuedConnection,
                                              Q_ARG(QStringList, successPaths));
                }

                if (error || !failedNames.isEmpty() || !canceledNames.isEmpty()) {
                    QString msg;
                    if (!deleteFailureSummary.isEmpty()) {
                        msg = deleteFailureSummary;
                    } else if (error) {
                        msg = QString::fromNSString(error.localizedDescription);
                    } else if (!failedNames.isEmpty()) {
                        msg = QString("Failed to delete: %1").arg(failedNames.join(", "));
                    } else {
                        msg = QString("Delete canceled: %1").arg(canceledNames.join(", "));
                    }
                    QMetaObject::invokeMethod(strongBridge, "deleteError",
                                              Qt::QueuedConnection,
                                              Q_ARG(QString, msg));
                }
                           }];
        return;
    }

    d->cameraDel.usingBlockDeleteAPI = NO;
    [camera requestDeleteFiles:icFiles];
}

// ============================================================
// Utility: CGImageRef → QPixmap
// ============================================================
static QPixmap cgImageToQPixmap(CGImageRef cgImg)
{
    if (!cgImg) return QPixmap();
    size_t w = CGImageGetWidth(cgImg);
    size_t h = CGImageGetHeight(cgImg);
    if (w == 0 || h == 0) return QPixmap();

    CGColorSpaceRef cs  = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    size_t bpr          = w * 4;
    std::vector<uint8_t> buf(h * bpr, 0);
    CGContextRef ctx = CGBitmapContextCreate(
        buf.data(), w, h, 8, bpr, cs,
        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host
    );
    CGColorSpaceRelease(cs);
    if (!ctx) return QPixmap();

    CGContextDrawImage(ctx, CGRectMake(0, 0, (CGFloat)w, (CGFloat)h), cgImg);
    CGContextRelease(ctx);

    // kCGBitmapByteOrder32Host + kCGImageAlphaPremultipliedFirst on little-endian = BGRA
    QImage img(buf.data(), (int)w, (int)h, (int)bpr,
               QImage::Format_ARGB32_Premultiplied);
    return QPixmap::fromImage(img.copy());
}

void ThetaBridge::requestImageInfo(const CameraFileInfo& file, quint64 requestId)
{
    ICCameraFile* native = (__bridge ICCameraFile*)file.nativeFileHandle;
    if (!d->cameraDel || !d->cameraDel.camera || !native || file.sizeBytes <= 0) {
        emit imageInfoFailed(file.devicePath, "The camera image is no longer available.", requestId);
        return;
    }
    const QPointer<ThetaBridge> guard(this);
    // EXIF y preview embebido de la Z1 caben en la cabecera: no copiar el RAW entero.
    const off_t length = std::min<qint64>(file.sizeBytes, 1024 * 1024);
    [native requestReadDataAtOffset:0 length:length completion:^(NSData* data, NSError* error) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!guard) return;
            if (error || !data.length) {
                emit guard->imageInfoFailed(file.devicePath, error
                    ? QString::fromNSString(error.localizedDescription)
                    : QString("The image header could not be read."), requestId);
                return;
            }
            CGImageSourceRef source = CGImageSourceCreateWithData((__bridge CFDataRef)data, nullptr);
            if (!source) {
                emit guard->imageInfoFailed(file.devicePath, "The image header is not readable.", requestId);
                return;
            }
            CameraFileInfo enriched = file;
            CFDictionaryRef properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr);
            if (properties) {
                NSDictionary* values = (__bridge NSDictionary*)properties;
                NSDictionary* exif = values[(__bridge NSString*)kCGImagePropertyExifDictionary];
                if (![exif isKindOfClass:[NSDictionary class]]) exif = nil;
                NSNumber* mode = exif[(__bridge NSString*)kCGImagePropertyExifExposureMode];
                if ([mode isKindOfClass:[NSNumber class]] && mode.intValue >= 0 && mode.intValue <= 2) enriched.exposureMode = mode.intValue;
                const auto number = [exif](CFStringRef key) {
                    id value = exif[(__bridge NSString*)key];
                    return [value isKindOfClass:[NSNumber class]] ? [value doubleValue] : 0.0;
                };
                const double seconds = number(kCGImagePropertyExifExposureTime);
                if (std::isfinite(seconds) && seconds > 0) enriched.exposureTime = seconds;
                const double aperture = number(kCGImagePropertyExifFNumber);
                NSArray* isoValues = exif[(__bridge NSString*)kCGImagePropertyExifISOSpeedRatings];
                const double iso = [isoValues isKindOfClass:[NSArray class]] && isoValues.count && [isoValues.firstObject isKindOfClass:[NSNumber class]]
                    ? [isoValues.firstObject doubleValue] : 0;
                if (std::isfinite(seconds) && std::isfinite(aperture) && std::isfinite(iso)
                    && seconds > 0 && aperture > 0 && iso > 0) {
                    enriched.exposureValue = std::log2(seconds) + std::log2(iso) - 2 * std::log2(aperture);
                }
                enriched.width = [values[(__bridge NSString*)kCGImagePropertyPixelWidth] intValue];
                enriched.height = [values[(__bridge NSString*)kCGImagePropertyPixelHeight] intValue];
                CFRelease(properties);
            }
            QPixmap thumbnail;
            if (file.isRaw) {
                NSDictionary* options = @{(__bridge NSString*)kCGImageSourceThumbnailMaxPixelSize:@1000,
                    (__bridge NSString*)kCGImageSourceCreateThumbnailFromImageIfAbsent:@YES,
                    (__bridge NSString*)kCGImageSourceCreateThumbnailWithTransform:@YES};
                CGImageRef image = CGImageSourceCreateThumbnailAtIndex(source, 0, (__bridge CFDictionaryRef)options);
                if (image) { thumbnail = cgImageToQPixmap(image); CGImageRelease(image); }
            }
            CFRelease(source);
            if (file.isRaw && thumbnail.isNull()) {
                emit guard->imageInfoFailed(file.devicePath, "The RAW embedded preview could not be read.", requestId);
            } else {
                emit guard->imageInfoReady(enriched, thumbnail, requestId);
            }
        });
    }];
}

static NSData* makePTPCommandData(uint16_t operationCode,
                                  uint32_t transactionId,
                                  std::initializer_list<uint32_t> params)
{
    const uint32_t length = 12u + static_cast<uint32_t>(params.size() * sizeof(uint32_t));
    NSMutableData* data = [NSMutableData dataWithLength:length];
    uint8_t* bytes = static_cast<uint8_t*>(data.mutableBytes);

    const uint32_t leLength = qToLittleEndian(length);
    const uint16_t leType = qToLittleEndian<uint16_t>(1); // command block
    const uint16_t leCode = qToLittleEndian(operationCode);
    const uint32_t leTx = qToLittleEndian(transactionId);

    std::memcpy(bytes + 0, &leLength, sizeof(leLength));
    std::memcpy(bytes + 4, &leType, sizeof(leType));
    std::memcpy(bytes + 6, &leCode, sizeof(leCode));
    std::memcpy(bytes + 8, &leTx, sizeof(leTx));

    size_t offset = 12;
    for (uint32_t param : params) {
        const uint32_t leParam = qToLittleEndian(param);
        std::memcpy(bytes + offset, &leParam, sizeof(leParam));
        offset += sizeof(leParam);
    }

    return data;
}

// ============================================================
// ThetaDeviceBrowserAdapter
// ============================================================
@implementation ThetaDeviceBrowserAdapter

- (void)deviceBrowser:(ICDeviceBrowser*)browser
         didAddDevice:(ICDevice*)addedDevice
           moreComing:(BOOL)moreComing
{
    NSString* devName = addedDevice.name ?: @"(unnamed)";
    NSString* devType = NSStringFromClass([addedDevice class]);
    LOGD("bridge") << "Device found:"
             << QString::fromNSString(devName)
             << "class:" << QString::fromNSString(devType)
             << "moreComing:" << moreComing;

    if (![addedDevice isKindOfClass:[ICCameraDevice class]]) {
        LOGD("bridge") << "not an ICCameraDevice, skipping";
        return;
    }

    ICCameraDevice* cam = (ICCameraDevice*)addedDevice;

    // ---- Device name filter ----
    // Only connect to the Ricoh Theta, not SD cards or other cameras.
    // The Theta always has "THETA" or "RICOH" in its device name.
    NSString* upperName = devName.uppercaseString;
    BOOL isTheta = [upperName containsString:@"THETA"] ||
                   [upperName containsString:@"RICOH"];

    if (!isTheta) {
        LOGD("bridge") << "not a Ricoh Theta (name="
                       << QString::fromNSString(devName) << "), skipping";
        return;
    }

    LOGI("bridge") << "Ricoh Theta identified. Connecting...";

    ThetaCameraDeviceAdapter* camDel = [[ThetaCameraDeviceAdapter alloc] init];
    camDel.qtBridge = self.qtBridge;
    camDel.camera   = cam;
    self.qtBridge->d->cameraDel = camDel;

    cam.delegate = camDel;
    [cam requestOpenSession];

    const QString serial = QString::fromNSString(cam.serialNumberString ?: @"").trimmed().toUpper();
    const QString identifier = serial.isEmpty() ? QString() : QStringLiteral("serial:") + serial;
    QMetaObject::invokeMethod(self.qtBridge, "cameraIdentityReady",
                              Qt::QueuedConnection,
                              Q_ARG(QString, identifier));
    QString name = QString::fromNSString(cam.name);
    QMetaObject::invokeMethod(self.qtBridge, "cameraConnected",
                              Qt::QueuedConnection,
                              Q_ARG(QString, name));
}

- (void)deviceBrowser:(ICDeviceBrowser*)browser
      didRemoveDevice:(ICDevice*)device
            moreGoing:(BOOL)moreGoing
{
    LOGI("bridge") << "Camera removed";
    QMetaObject::invokeMethod(self.qtBridge, "cameraDisconnected",
                              Qt::QueuedConnection);
}

- (void)deviceBrowserDidEnumerateLocalDevices:(ICDeviceBrowser*)browser
{
    LOGD("bridge") << "Local device enumeration complete";
}

@end

// ============================================================
// ThetaCameraDeviceAdapter
// ============================================================
@implementation ThetaCameraDeviceAdapter

// ---- Debounce helper ----
// Cancels any pending enumeration and schedules a new one 300ms from now.
// All callbacks that used to call [self enumerateFiles] now call this instead.
- (void)scheduleEnumeration
{
    if (self.pendingEnumeration) {
        dispatch_block_cancel(self.pendingEnumeration);
    }
    __weak typeof(self) weakSelf = self;
    dispatch_block_t block = dispatch_block_create(DISPATCH_BLOCK_INHERIT_QOS_CLASS, ^{
        [weakSelf enumerateFiles];
    });
    self.pendingEnumeration = block;
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)(300 * NSEC_PER_MSEC)),
        dispatch_get_main_queue(),
        block
    );
}

- (void)recoverSessionForDownloads
{
    if (!self.camera) {
        LOGW("bridge") << "recoverSessionForDownloads: no camera";
        return;
    }

    if (self.recoveringDownloadSession || self.reopenSessionAfterClose) {
        LOGD("bridge") << "recoverSessionForDownloads already in progress";
        return;
    }

    LOGW("bridge") << "Closing session to recover download pipeline";
    self.recoveringDownloadSession = YES;
    self.reopenSessionAfterClose = YES;
    [self.camera requestCloseSession];
}

- (void)emitBatteryStatus
{
    if (!self.qtBridge || !self.camera) {
        return;
    }

    bool available = self.camera.batteryLevelAvailable;
    int percent = available ? (int)self.camera.batteryLevel : -1;
    if (!available && self.hasPTPBatteryLevel) {
        available = YES;
        percent = self.ptpBatteryLevel;
    }
    LOGD("bridge") << "Battery status:"
                   << "available:" << available
                   << "percent:" << percent;
    QMetaObject::invokeMethod(self.qtBridge, "batteryLevelChanged",
                              Qt::QueuedConnection,
                              Q_ARG(int, percent),
                              Q_ARG(bool, available));

    if (!available) {
        [self requestBatteryStatusViaPTPIfNeeded];
    }
}

- (void)requestBatteryStatusViaPTPIfNeeded
{
    if (!self.camera || self.pendingBatteryQuery) {
        return;
    }

    NSArray<NSString*>* capabilities = self.camera.capabilities ?: @[];
    if (![capabilities containsObject:ICCameraDeviceCanAcceptPTPCommands]) {
        LOGD("bridge") << "PTP battery query skipped: camera does not advertise PTP command capability";
        return;
    }

    self.pendingBatteryQuery = YES;
    const uint32_t txBase = ++self.ptpTransactionId;
    NSData* batteryLevelCommand = makePTPCommandData(0x1015, txBase, {0x5001});
    NSData* batteryStatusCommand = makePTPCommandData(0x1015, txBase + 1, {0xD80C});

    LOGI("bridge") << "Requesting battery via PTP"
                   << "tx:" << txBase
                   << "camera:" << QString::fromNSString(self.camera.name ?: @"");

    __weak typeof(self) weakSelf = self;
    [self.camera requestSendPTPCommand:batteryLevelCommand
                               outData:nil
                            completion:^(NSData* responseData, NSData* ptpResponseData, NSError* error) {
        __strong typeof(weakSelf) strongSelf = weakSelf;
        if (!strongSelf) {
            return;
        }

        if (error) {
            strongSelf.pendingBatteryQuery = NO;
            LOGW("bridge") << "PTP battery level query failed:"
                           << QString::fromNSString(error.localizedDescription)
                           << "code:" << error.code;
            return;
        }

        int percent = -1;
        if (responseData.length >= 1) {
            const uint8_t* bytes = static_cast<const uint8_t*>(responseData.bytes);
            percent = static_cast<int>(bytes[0]);
        }

        LOGI("bridge") << "PTP battery level result:"
                       << percent
                       << "responseBytes:" << (int)responseData.length
                       << "dataBytes:" << (int)ptpResponseData.length;

        if (percent >= 0 && percent <= 100) {
            strongSelf.hasPTPBatteryLevel = YES;
            strongSelf.ptpBatteryLevel = percent;
            QMetaObject::invokeMethod(strongSelf.qtBridge, "batteryLevelChanged",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, percent),
                                      Q_ARG(bool, true));
        }

        [strongSelf.camera requestSendPTPCommand:batteryStatusCommand
                                         outData:nil
                                      completion:^(NSData* statusResponse, NSData* statusData, NSError* statusError) {
            strongSelf.pendingBatteryQuery = NO;

            if (statusError) {
                LOGW("bridge") << "PTP battery status query failed:"
                               << QString::fromNSString(statusError.localizedDescription)
                               << "code:" << statusError.code;
                return;
            }

            int status = -1;
            if (statusResponse.length >= 1) {
                const uint8_t* statusBytes = static_cast<const uint8_t*>(statusResponse.bytes);
                status = static_cast<int>(statusBytes[0]);
            }
            LOGI("bridge") << "PTP battery status result:"
                           << status
                           << "responseBytes:" << (int)statusResponse.length
                           << "dataBytes:" << (int)statusData.length;
        }];
    }];
}

// ---- ICDeviceDelegate REQUIRED ----

- (void)device:(ICDevice*)device didOpenSessionWithError:(NSError* _Nullable)error
{
    if (error) {
        LOGW("bridge") << "Session open error:"
                   << QString::fromNSString(error.localizedDescription);
        self.recoveringDownloadSession = NO;
        self.reopenSessionAfterClose = NO;
        QMetaObject::invokeMethod(self.qtBridge, "cameraError",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, QString::fromNSString(error.localizedDescription)));
    } else {
        LOGI("bridge") << "Session opened OK; scheduling enumeration";
        if (self.recoveringDownloadSession) {
            LOGI("bridge") << "Download session recovery completed";
            self.recoveringDownloadSession = NO;
        }
        [self emitBatteryStatus];
        // NOTE: do NOT call requestEnableTethering here.
        // It triggers cameraDeviceDidChangeCapability: callbacks which
        // were causing the burst-enumeration hang.
        [self scheduleEnumeration];
    }
}

- (void)device:(ICDevice*)device didCloseSessionWithError:(NSError* _Nullable)error
{
    LOGI("bridge") << "Session closed";
    if (self.pendingEnumeration) {
        dispatch_block_cancel(self.pendingEnumeration);
        self.pendingEnumeration = nil;
    }
    if (self.reopenSessionAfterClose && self.camera) {
        self.reopenSessionAfterClose = NO;
        LOGI("bridge") << "Reopening session after recovery close";
        dispatch_after(
            dispatch_time(DISPATCH_TIME_NOW, (int64_t)(350 * NSEC_PER_MSEC)),
            dispatch_get_main_queue(),
            ^{
                [self.camera requestOpenSession];
            }
        );
    }
}

- (void)didRemoveDevice:(ICDevice*)device
{
    LOGI("bridge") << "Device removed";
    if (self.pendingEnumeration) {
        dispatch_block_cancel(self.pendingEnumeration);
        self.pendingEnumeration = nil;
    }
    self.reopenSessionAfterClose = NO;
    self.recoveringDownloadSession = NO;
    [self emitBatteryStatus];
    QMetaObject::invokeMethod(self.qtBridge, "cameraDisconnected",
                              Qt::QueuedConnection);
}

// ---- ICDeviceDelegate optional ----

- (void)device:(ICDevice*)device didEncounterError:(NSError*)error
{
    QString msg = QString::fromNSString(error.localizedDescription);
    LOGW("bridge") << "Device error:" << msg;
    QMetaObject::invokeMethod(self.qtBridge, "cameraError",
                              Qt::QueuedConnection,
                              Q_ARG(QString, msg));
}

// ---- ICCameraDeviceDelegate REQUIRED ----

- (void)cameraDevice:(ICCameraDevice*)camera
    didReceiveThumbnail:(CGImageRef _Nullable)thumbnail
               forItem:(ICCameraItem*)item
                 error:(NSError* _Nullable)error
{
    if (!thumbnail || error) {
        if (error) {
            LOGW("bridge") << "Thumbnail error for" << QString::fromNSString(item.name)
                       << ":" << QString::fromNSString(error.localizedDescription);
        }
        NSString* pathNS = item.fileSystemPath;
        const QString path = QString::fromNSString(pathNS ?: item.name);
        QMetaObject::invokeMethod(self.qtBridge, "thumbnailFailed", Qt::QueuedConnection, Q_ARG(QString, path));
        return;
    }
    QPixmap pixmap = cgImageToQPixmap(thumbnail);
    if (pixmap.isNull()) return;

    NSString* pathNS = item.fileSystemPath;
    QString path = pathNS ? QString::fromNSString(pathNS)
                           : QString::fromNSString(item.name);

    QMetaObject::invokeMethod(self.qtBridge, "thumbnailReady",
                              Qt::QueuedConnection,
                              Q_ARG(QString, path),
                              Q_ARG(QPixmap, pixmap));
}

- (void)cameraDevice:(ICCameraDevice*)camera didRenameItems:(NSArray<ICCameraItem*>*)items
{
    LOGD("bridge") << "Items renamed";
    [self scheduleEnumeration];
}

- (void)cameraDevice:(ICCameraDevice*)camera didReceivePTPEvent:(NSData*)eventData
{
    // PTP pass-through events not needed for file browsing
}

// ---- ICCameraDeviceDelegate optional ----

- (void)cameraDeviceDidChangeCapability:(ICCameraDevice*)camera
{
    LOGD("bridge") << "Capability changed; scheduling enumeration";
    [self emitBatteryStatus];
    [self scheduleEnumeration];
}

- (void)cameraDevice:(ICCameraDevice*)camera didAddItems:(NSArray<ICCameraItem*>*)items
{
    LOGD("bridge") << "Items added:" << (int)items.count << "; scheduling enumeration";
    [self scheduleEnumeration];
}

- (void)cameraDevice:(ICCameraDevice*)camera didRemoveItems:(NSArray<ICCameraItem*>*)items
{
    LOGD("bridge") << "Items removed:" << (int)items.count;
    [self scheduleEnumeration];
}

- (void)deviceDidBecomeReadyWithCompleteContentCatalog:(ICCameraDevice*)device
{
    LOGD("bridge") << "Catalog complete; scheduling enumeration";
    [self emitBatteryStatus];
    [self scheduleEnumeration];
}

- (void)cameraDevice:(ICCameraDevice*)camera
    didReceiveSessionOptions:(NSDictionary<NSString*,id>*)options
{
    LOGD("bridge") << "Session options received; scheduling enumeration";
    [self emitBatteryStatus];
    [self scheduleEnumeration];
}

- (void)deviceDidBecomeReady:(ICDevice*)device
{
    LOGD("bridge") << "Device became ready";
    [self emitBatteryStatus];
}

- (void)device:(ICDevice*)device didReceiveStatusInformation:(NSDictionary<ICDeviceStatus,id>*)status
{
    Q_UNUSED(status);
    LOGD("bridge") << "Device status information received";
    [self emitBatteryStatus];
}

// Stub to suppress compiler warnings — we don't need metadata for file listing
- (void)cameraDevice:(ICCameraDevice*)camera
    didReceiveMetadata:(NSDictionary* _Nullable)metadata
               forItem:(ICCameraItem*)item
                 error:(NSError* _Nullable)error
{
    // Metadata arrives asynchronously. We don't request it explicitly,
    // but the camera sends it anyway. Ignore.
}

- (void)cameraDeviceDidRemoveAccessRestriction:(ICDevice*)device { }
- (void)cameraDeviceDidEnableAccessRestriction:(ICDevice*)device { }

// ---- File enumeration (runs once, after debounce settles) ----

- (void)enumerateFiles
{
    self.pendingEnumeration = nil;
    if (!self.camera) return;

    NSArray<ICCameraItem*>* allItems = self.camera.mediaFiles;
    if (!allItems) allItems = @[];

    QList<CameraFileInfo> fileList;
    fileList.reserve((int)allItems.count);

    for (ICCameraItem* item in allItems) {
        if (![item isKindOfClass:[ICCameraFile class]]) continue;
        ICCameraFile* f = (ICCameraFile*)item;

        CameraFileInfo info;
        info.name       = QString::fromNSString(f.name);
        info.uti        = f.UTI ? QString::fromNSString(f.UTI) : QString();

        // fileSystemPath is the macOS-side unique identifier for the file on the device
        NSString* syspath   = f.fileSystemPath;
        info.devicePath     = syspath ? QString::fromNSString(syspath)
                                      : QString::fromNSString(f.name);

        info.sizeBytes      = (qint64)f.fileSize;
        // width/height: don't access metadataIfAvailable here — it triggers extra
        // callbacks that can restart the enumeration cascade.
        // We'll get dimensions from EXIF after thumbnail arrives.
        info.width  = 0;
        info.height = 0;

        NSString* uti = f.UTI ?: @"";
        NSString* ext = f.name.pathExtension.lowercaseString ?: @"";

        info.isVideo = [uti hasPrefix:@"public.movie"]        ||
                       [uti isEqualToString:@"public.mpeg-4"] ||
                       [uti hasPrefix:@"public.video"]         ||
                       [ext isEqualToString:@"mp4"]            ||
                       [ext isEqualToString:@"mov"];

        info.isRaw   = [uti isEqualToString:@"com.adobe.raw-image"] ||
                       [uti hasSuffix:@"raw-image"]                   ||
                       [ext isEqualToString:@"dng"]                   ||
                       [ext isEqualToString:@"arw"]                   ||
                       [ext isEqualToString:@"cr2"]                   ||
                       [ext isEqualToString:@"nef"];

        if (f.creationDate) {
            NSTimeInterval ti = [f.creationDate timeIntervalSince1970];
            info.creationDate = QDateTime::fromSecsSinceEpoch((qint64)ti);
        }

        // Store raw Obj-C pointer as void*.
        // ARC keeps ICCameraFile alive through camera.mediaFiles (NSArray retains it).
        info.nativeFileHandle = (__bridge void*)f;
        fileList.append(info);
    }

    LOGI("bridge") << "enumerateFiles emitting" << fileList.size() << "files";
    QMetaObject::invokeMethod(self.qtBridge, "fileListUpdated",
                              Qt::QueuedConnection,
                              Q_ARG(QList<CameraFileInfo>, fileList));
}

// ---- Download progress ----

- (void)cameraDevice:(ICCameraDevice*)camera
    didReceiveDownloadProgressForFile:(ICCameraFile*)file
                      downloadedBytes:(off_t)downloadedBytes
                             maxBytes:(off_t)maxBytes
{
    int pct = (maxBytes > 0) ? (int)(downloadedBytes * 100 / maxBytes) : 0;
    QString name = QString::fromNSString(file.name);
    QMetaObject::invokeMethod(self.qtBridge, "downloadProgress",
                              Qt::QueuedConnection,
                              Q_ARG(QString, name),
                              Q_ARG(int, pct));
}

// ---- Download completion selector ----

- (void)didDownloadFile:(ICCameraFile*)file
                  error:(NSError*)error
                options:(NSDictionary*)options
            contextInfo:(void*)contextInfo
{
    QString name = QString::fromNSString(file.name);
    NSString* savedName = options ? options[ICSavedFilename] : nil;
    NSString* savedDir = nil;
    id dirUrl = options ? options[ICDownloadsDirectoryURL] : nil;
    if ([dirUrl isKindOfClass:[NSURL class]]) {
        savedDir = [(NSURL*)dirUrl path];
    }
    if (error) {
        QString msg = QString::fromNSString(error.localizedDescription);
        LOGW("bridge") << "Download error"
                       << name
                       << "savedName:" << (savedName ? QString::fromNSString(savedName) : QString())
                       << "destDir:" << (savedDir ? QString::fromNSString(savedDir) : QString())
                       << "code:" << error.code
                       << "domain:" << QString::fromNSString(error.domain ?: @"")
                       << "message:" << msg;
        QMetaObject::invokeMethod(self.qtBridge, "downloadError",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, name),
                                  Q_ARG(QString, msg));
    } else {
        QString savedPath   = savedName ? QString::fromNSString(savedName) : QString();
        LOGI("bridge") << "Downloaded:"
                       << name
                       << "->" << savedPath
                       << "destDir:" << (savedDir ? QString::fromNSString(savedDir) : QString());
        QMetaObject::invokeMethod(self.qtBridge, "downloadFileCompleted",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, name),
                                  Q_ARG(QString, savedPath));
    }
}

// ---- Delete completion ----

- (void)cameraDevice:(ICCameraDevice*)camera
      didDeleteFiles:(NSArray<ICCameraItem*>*)deletedFiles
               error:(NSError*)error
{
    if (self.usingBlockDeleteAPI) {
        return;
    }

    if (error) {
        QString msg = QString::fromNSString(error.localizedDescription);
        LOGW("bridge") << "Delete error:" << msg;
        QMetaObject::invokeMethod(self.qtBridge, "deleteError",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, msg));
    } else {
        QStringList paths;
        for (ICCameraItem* item in deletedFiles) {
            NSString* p = item.fileSystemPath;
            paths << (p ? QString::fromNSString(p) : QString::fromNSString(item.name));
        }
        LOGI("bridge") << "Deleted:" << paths;
        QMetaObject::invokeMethod(self.qtBridge, "deleteCompleted",
                                  Qt::QueuedConnection,
                                  Q_ARG(QStringList, paths));
    }
}

- (void)cameraDevice:(ICCameraDevice*)camera
didCompleteDeleteFilesWithError:(NSError* _Nullable)error
{
    if (self.usingBlockDeleteAPI) {
        return;
    }

    if (error) {
        const QString msg = QString::fromNSString(error.localizedDescription);
        LOGW("bridge") << "didCompleteDeleteFilesWithError:"
                       << msg
                       << "code:" << error.code
                       << "domain:" << QString::fromNSString(error.domain ?: @"");
        QMetaObject::invokeMethod(self.qtBridge, "deleteError",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, msg));
    } else {
        LOGI("bridge") << "didCompleteDeleteFilesWithError: success";
    }
}

@end
