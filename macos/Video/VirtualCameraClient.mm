#import "VirtualCameraClient.h"
#import <CoreMediaIO/CoreMediaIO.h>
#include <vector>

static CMIOObjectPropertyAddress Address(CMIOObjectPropertySelector selector, CMIOObjectPropertyScope scope = kCMIOObjectPropertyScopeGlobal) {
    return {selector, scope, kCMIOObjectPropertyElementMain};
}
static std::vector<CMIOObjectID> Objects(CMIOObjectID object, CMIOObjectPropertyAddress address) {
    UInt32 size = 0, used = 0;
    if (CMIOObjectGetPropertyDataSize(object, &address, 0, nullptr, &size) != noErr) return {};
    std::vector<CMIOObjectID> objects(size / sizeof(CMIOObjectID));
    if (!size || CMIOObjectGetPropertyData(object, &address, 0, nullptr, size, &used, objects.data()) != noErr) return {};
    objects.resize(used / sizeof(CMIOObjectID)); return objects;
}
static NSString *StringProperty(CMIOObjectID object, CMIOObjectPropertySelector selector) {
    auto address = Address(selector); CFStringRef value = nullptr; UInt32 used = 0;
    if (CMIOObjectGetPropertyData(object, &address, 0, nullptr, sizeof(value), &used, &value) != noErr || !value) return nil;
    return CFBridgingRelease(value);
}

@implementation CLVirtualCameraClient {
    CMIODeviceID _device;
    CMIOStreamID _sink;
    CMSimpleQueueRef _queue;
    NSInteger _consumerCount;
    NSString *_status;
}
- (instancetype)init {
    if ((self = [super init])) { _status = @"Choose Camera setup to install or update Callayers."; }
    return self;
}
- (NSInteger)consumerCount { return _consumerCount; }
- (NSString *)status { return _status; }
- (void)refresh {
    // Include software devices in the legacy C API's device list.
    auto allow = Address(kCMIOHardwarePropertyAllowScreenCaptureDevices); UInt32 yes = 1;
    CMIOObjectSetPropertyData(kCMIOObjectSystemObject, &allow, 0, nullptr, sizeof(yes), &yes);
    CMIODeviceID found = 0;
    for (auto device : Objects(kCMIOObjectSystemObject, Address(kCMIOHardwarePropertyDevices))) {
        if ([StringProperty(device, kCMIODevicePropertyDeviceUID) isEqualToString:@"com.callayers.virtual-camera"]) { found = device; break; }
    }
    if (!found || (_device && _device != found)) { [self stop]; _device = 0; _sink = 0; }
    _consumerCount = 0;
    if (!found) { _status = @"Callayers camera is not active. Open Camera setup."; return; }
    _device = found;
    NSString *count = StringProperty(_device, 'cact');
    if (!count) { [self stop]; _sink = 0; _status = @"Camera update needed. Open Camera setup and install the new version."; return; }
    _consumerCount = MAX(0, count.integerValue);
    CMIOStreamID newSink = 0;
    for (auto stream : Objects(_device, Address(kCMIODevicePropertyStreams, kCMIODevicePropertyScopeOutput))) {
        newSink = stream; break;
    }
    if (_sink != newSink) { [self stop]; _sink = newSink; }
    if (!_sink) { [self stop]; _status = @"Camera input is unavailable. Reinstall from Camera setup."; return; }
    if (!_consumerCount) { [self stop]; _status = @"Callayers camera ready — select it in your calling app."; }
    else { _status = @"Sending workspace to Callayers camera."; }
}
- (BOOL)sendSample:(CMSampleBufferRef)sample {
    if (!_consumerCount || !_sink) return NO;
    if (!_queue) {
        OSStatus result = CMIOStreamCopyBufferQueue(_sink, nullptr, nullptr, &_queue);
        if (result == noErr) result = CMIODeviceStartStream(_device, _sink);
        if (result != noErr) {
            [self stop]; _status = [NSString stringWithFormat:@"Could not connect to Callayers camera (%d).", (int)result]; return NO;
        }
    }
    if (CMSimpleQueueGetCount(_queue) >= MIN(2, CMSimpleQueueGetCapacity(_queue))) return NO;
    CFRetain(sample);
    const OSStatus result = CMSimpleQueueEnqueue(_queue, sample);
    if (result != noErr) { CFRelease(sample); return NO; }
    return YES;
}
- (void)stop {
    if (!_queue) return;
    CMIODeviceStopStream(_device, _sink);
    while (const void *sample = CMSimpleQueueDequeue(_queue)) CFRelease(sample);
    CFRelease(_queue); _queue = nullptr;
}
- (void)dealloc { [self stop]; }
@end
