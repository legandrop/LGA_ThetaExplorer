// ThetaBridgePrivate.h
// Only ever #included inside ThetaBridge.mm - never from a .h or .cpp file.
// Contains Objective-C class declarations that cannot appear in plain C++ headers.

#import <Foundation/Foundation.h>
#import <ImageCaptureCore/ImageCaptureCore.h>
#import <dispatch/dispatch.h>
#include "thetaexplorer/ThetaBridge.h"

// ---- Adapter: ICDeviceBrowser delegate ----
@interface ThetaDeviceBrowserAdapter : NSObject <ICDeviceBrowserDelegate>
@property (nonatomic, assign) ThetaBridge* qtBridge;  // raw ptr - Qt owns the object
@end

// ---- Adapter: ICCameraDevice + download delegate ----
@interface ThetaCameraDeviceAdapter : NSObject <ICCameraDeviceDelegate, ICCameraDeviceDownloadDelegate>
@property (nonatomic, assign) ThetaBridge*    qtBridge;
@property (nonatomic, strong) ICCameraDevice* camera;

// GCD block used to debounce enumerateFiles calls.
// All callbacks that trigger enumeration cancel+reschedule this instead of
// calling enumerateFiles directly. Result: exactly ONE enumeration fires after
// the burst of ImageCaptureCore init callbacks settles (~300ms).
@property (nonatomic, copy)   dispatch_block_t pendingEnumeration;
@property (nonatomic, assign) BOOL reopenSessionAfterClose;
@property (nonatomic, assign) BOOL recoveringDownloadSession;
@property (nonatomic, assign) BOOL pendingBatteryQuery;
@property (nonatomic, assign) uint32_t ptpTransactionId;
@property (nonatomic, assign) BOOL hasPTPBatteryLevel;
@property (nonatomic, assign) int ptpBatteryLevel;
@property (nonatomic, strong) NSProgress* activeDeleteProgress;
@property (nonatomic, assign) BOOL usingBlockDeleteAPI;

- (void)scheduleEnumeration;  // debounced entry point
- (void)enumerateFiles;        // actual work, runs on main queue after debounce
- (void)recoverSessionForDownloads;
- (void)emitBatteryStatus;
- (void)requestBatteryStatusViaPTPIfNeeded;
@end
