#import "../Video/VirtualCameraClient.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#include <atomic>

// Signed integration check for the app transport and installed extension.
// Run with Callayers closed and camera permission already granted. Sends only a
// synthetic marker and verifies public AVFoundation output; stores no images.
@interface CameraMarkerReader : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, readonly) unsigned matches;
@end
@implementation CameraMarkerReader {
    std::atomic<unsigned> _matches;
}
- (unsigned)matches { return _matches.load(); }
- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)sample
       fromConnection:(AVCaptureConnection *)connection {
    CVPixelBufferRef image = CMSampleBufferGetImageBuffer(sample);
    if (!image || CVPixelBufferGetPixelFormatType(image) != kCVPixelFormatType_32BGRA) return;
    CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    const auto p = (const uint8_t *)CVPixelBufferGetBaseAddress(image);
    if (p && p[0] == 31 && p[1] == 173 && p[2] == 229 && p[3] == 255) ++_matches;
    CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
}
@end

int main() {
    @autoreleasepool {
        if ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo] != AVAuthorizationStatusAuthorized) {
            fprintf(stderr, "Camera permission for the signed Callayers identity is required.\n"); return 2;
        }
        AVCaptureDevice *camera = nil;
        NSArray<AVCaptureDeviceType> *types;
        if (@available(macOS 14.0, *)) types = @[AVCaptureDeviceTypeExternal];
        else types = @[AVCaptureDeviceTypeExternalUnknown];
        AVCaptureDeviceDiscoverySession *discovery = [AVCaptureDeviceDiscoverySession
            discoverySessionWithDeviceTypes:types mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionUnspecified];
        for (AVCaptureDevice *candidate in discovery.devices) {
            if ([candidate.uniqueID isEqualToString:@"com.callayers.virtual-camera"]) { camera = candidate; break; }
        }
        NSError *error = nil;
        AVCaptureDeviceInput *input = camera ? [AVCaptureDeviceInput deviceInputWithDevice:camera error:&error] : nil;
        AVCaptureSession *session = [AVCaptureSession new];
        if (!input || ![session canAddInput:input]) { fprintf(stderr,"Camera input unavailable\n"); return 2; }
        [session addInput:input];
        CameraMarkerReader *reader = [CameraMarkerReader new];
        AVCaptureVideoDataOutput *output = [AVCaptureVideoDataOutput new];
        output.videoSettings = @{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)};
        output.alwaysDiscardsLateVideoFrames = YES;
        [output setSampleBufferDelegate:reader queue:dispatch_queue_create("com.callayers.transport-check", DISPATCH_QUEUE_SERIAL)];
        if (![session canAddOutput:output]) return 2;
        [session addOutput:output];
        [session startRunning];
        CLVirtualCameraClient *client = [CLVirtualCameraClient new];
        CVPixelBufferRef pixels = nullptr;
        NSDictionary *attributes = @{(id)kCVPixelBufferIOSurfacePropertiesKey: @{}};
        CVReturn allocated = CVPixelBufferCreate(nullptr, 1920, 1080, kCVPixelFormatType_32BGRA,
            (__bridge CFDictionaryRef)attributes, &pixels);
        if (allocated) { [session stopRunning]; return 1; }
        CVPixelBufferLockBaseAddress(pixels, 0);
        for (size_t y = 0; y < 1080; ++y) {
            auto row = (uint8_t *)CVPixelBufferGetBaseAddress(pixels) + y * CVPixelBufferGetBytesPerRow(pixels);
            for (size_t x = 0; x < 1920; ++x) {
                row[x*4] = 31; row[x*4+1] = 173; row[x*4+2] = 229; row[x*4+3] = 255;
            }
        }
        CVPixelBufferUnlockBaseAddress(pixels, 0);
        CMVideoFormatDescriptionRef format = nullptr;
        CMVideoFormatDescriptionCreateForImageBuffer(nullptr, pixels, &format);
        unsigned sent = 0;
        CFAbsoluteTime deadline = CFAbsoluteTimeGetCurrent() + 5;
        while (CFAbsoluteTimeGetCurrent() < deadline && reader.matches < 10) {
            [client refresh];
            CMSampleTimingInfo timing = {CMTimeMake(1,30), CMClockGetTime(CMClockGetHostTimeClock()), kCMTimeInvalid};
            CMSampleBufferRef sample = nullptr;
            if (!CMSampleBufferCreateReadyWithImageBuffer(nullptr, pixels, format, &timing, &sample)) {
                if ([client sendSample:sample]) ++sent;
                CFRelease(sample);
            }
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1.0/30.0]];
        }
        [client stop]; [session stopRunning];
        CFRelease(format); CFRelease(pixels);
        printf("%s source round trip: %u sent, %u matching frames received; %s\n",
            reader.matches >= 10 ? "PASS" : "FAIL", sent, reader.matches, client.status.UTF8String);
        return reader.matches >= 10 ? 0 : 1;
    }
}
