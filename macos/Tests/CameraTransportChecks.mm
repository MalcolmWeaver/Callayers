#import "CameraTransportChecks.h"
#import "../Video/VirtualCameraClient.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#include <atomic>
#include <algorithm>
#include <vector>

@interface CameraMarkerReader : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, readonly) unsigned changes;
@property(nonatomic, readonly) unsigned fallbackFrames;
@end
@implementation CameraMarkerReader {
    std::atomic<unsigned> _changes, _fallbackFrames;
    int _previous;
}
- (instancetype)init { if ((self = [super init])) _previous = -1; return self; }
- (unsigned)changes { return _changes.load(); }
- (unsigned)fallbackFrames { return _fallbackFrames.load(); }
- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)sample
       fromConnection:(AVCaptureConnection *)connection {
    CVPixelBufferRef image = CMSampleBufferGetImageBuffer(sample);
    if (!image || CVPixelBufferGetPixelFormatType(image) != kCVPixelFormatType_32BGRA ||
        CVPixelBufferGetWidth(image) != 1920 || CVPixelBufferGetHeight(image) != 1080) return;
    CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    const auto p = (const uint8_t *)CVPixelBufferGetBaseAddress(image);
    if (p && p[0] == 31 && p[1] == 173 && p[2] >= 220 && p[2] <= 223 && p[3] == 255) {
        if (_previous != p[2]) ++_changes;
        _previous = p[2];
    // DeviceRGB conversion varies; accept the slate navy, excluding black and markers.
    } else if (p && p[0] >= 20 && p[0] <= 50 && p[1] >= 10 && p[1] <= 30 && p[2] <= 20 && p[3] == 255) {
        ++_fallbackFrames;
        _previous = -1;
    }
    CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
}
@end

NSDictionary *CLRunCameraTransportChecks(double duration) {
    @autoreleasepool {
        auto failure = ^NSDictionary *(NSString *message) { return @{@"passed": @NO, @"error": message}; };
        if ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo] != AVAuthorizationStatusAuthorized)
            return failure(@"Open Callayers normally and grant camera permission before unattended testing.");
        AVCaptureDevice *camera = nil;
        NSArray<AVCaptureDeviceType> *types;
        if (@available(macOS 14.0, *)) types = @[AVCaptureDeviceTypeExternal];
        else types = @[AVCaptureDeviceTypeExternalUnknown];
        AVCaptureDeviceDiscoverySession *discovery = [AVCaptureDeviceDiscoverySession
            discoverySessionWithDeviceTypes:types mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionUnspecified];
        for (AVCaptureDevice *candidate in discovery.devices)
            if ([candidate.uniqueID isEqualToString:@"com.callayers.virtual-camera"]) { camera = candidate; break; }
        NSError *error = nil;
        AVCaptureDeviceInput *input = camera ? [AVCaptureDeviceInput deviceInputWithDevice:camera error:&error] : nil;
        AVCaptureSession *session = [AVCaptureSession new];
        if (!input || ![session canAddInput:input]) return failure(error.localizedDescription ?: @"Callayers camera is not installed.");
        [session addInput:input];
        CameraMarkerReader *reader = [CameraMarkerReader new];
        AVCaptureVideoDataOutput *output = [AVCaptureVideoDataOutput new];
        output.videoSettings = @{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)};
        output.alwaysDiscardsLateVideoFrames = YES;
        dispatch_queue_t captureQueue = dispatch_queue_create("com.callayers.transport-check", DISPATCH_QUEUE_SERIAL);
        [output setSampleBufferDelegate:reader queue:captureQueue];
        if (![session canAddOutput:output]) return failure(@"Cannot read virtual camera output.");
        [session addOutput:output];
        // Four immutable buffers: queued frames are never mutated or accumulated.
        std::vector<CVPixelBufferRef> buffers;
        for (unsigned index = 0; index < 4; ++index) {
            CVPixelBufferRef pixels = nullptr;
            NSDictionary *attributes = @{(id)kCVPixelBufferIOSurfacePropertiesKey: @{}};
            if (CVPixelBufferCreate(nullptr, 1920, 1080, kCVPixelFormatType_32BGRA,
                (__bridge CFDictionaryRef)attributes, &pixels)) {
                for (auto buffer : buffers) CFRelease(buffer);
                return failure(@"Synthetic frame allocation failed.");
            }
            CVPixelBufferLockBaseAddress(pixels, 0);
            for (size_t y = 0; y < 1080; ++y) {
                auto row = (uint8_t *)CVPixelBufferGetBaseAddress(pixels) + y * CVPixelBufferGetBytesPerRow(pixels);
                for (size_t x = 0; x < 1920; ++x) {
                    row[x*4] = 31; row[x*4+1] = 173; row[x*4+2] = 220 + index; row[x*4+3] = 255;
                }
            }
            CVPixelBufferUnlockBaseAddress(pixels, 0);
            buffers.push_back(pixels);
        }
        CMVideoFormatDescriptionRef format = nullptr;
        OSStatus formatted = CMVideoFormatDescriptionCreateForImageBuffer(nullptr, buffers[0], &format);
        if (formatted) {
            for (auto buffer : buffers) CFRelease(buffer);
            return failure(@"Synthetic frame format failed.");
        }
        CLVirtualCameraClient *client = [CLVirtualCameraClient new];
        NSMutableArray *checks = [NSMutableArray new];
        unsigned sequence = 0;
        auto run = [&](NSString *name, double seconds, bool sending) {
            unsigned before = sending ? reader.changes : reader.fallbackFrames;
            unsigned last = before, sent = 0;
            double start = CMTimeGetSeconds(CMClockGetTime(CMClockGetHostTimeClock()));
            double lastChange = start, maxGap = 0;
            while (true) {
                @autoreleasepool {
                    double now = CMTimeGetSeconds(CMClockGetTime(CMClockGetHostTimeClock()));
                    if (now - start >= seconds) break;
                    [client refresh];
                    if (sending) {
                        CMSampleTimingInfo timing = {CMTimeMake(1,30), CMClockGetTime(CMClockGetHostTimeClock()), kCMTimeInvalid};
                        CMSampleBufferRef sample = nullptr;
                        if (!CMSampleBufferCreateReadyWithImageBuffer(nullptr, buffers[sequence++ % 4], format, &timing, &sample)) {
                            if ([client sendSample:sample]) ++sent;
                            CFRelease(sample);
                        }
                    }
                    unsigned current = sending ? reader.changes : reader.fallbackFrames;
                    if (current != last) { lastChange = now; last = current; }
                    maxGap = std::max(maxGap, now - lastChange);
                    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1.0/30.0]];
                }
            }
            unsigned received = (sending ? reader.changes : reader.fallbackFrames) - before;
            BOOL passed = received >= (sending ? seconds * 10 : 10) && maxGap < 3;
            [checks addObject:@{@"name": name, @"passed": @(passed), @"sent": @(sent),
                @"observed": @(received), @"maximumGapSeconds": @(maxGap), @"transport": client.status}];
        };
        [session startRunning];
        run(@"changing synthetic frames", duration, true);
        run(@"stale input becomes fallback", 4, false);
        [client stop];
        run(@"producer reconnect", 6, true);
        [client stop];
        [session stopRunning];
        [session startRunning];
        run(@"consumer restart", 6, true);
        [client stop];
        [session stopRunning];
        [output setSampleBufferDelegate:nil queue:nullptr];
        dispatch_sync(captureQueue, ^{});
        CFRelease(format);
        for (auto buffer : buffers) CFRelease(buffer);
        BOOL passed = YES;
        for (NSDictionary *check in checks) passed = passed && [check[@"passed"] boolValue];
        return @{@"passed": @(passed), @"checks": checks, @"physicalCameraOpened": @NO};
    }
}
