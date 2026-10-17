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
// Registering interest makes CMIO keep its cached custom property current.
// The main-thread refresh reads the value; this callback never touches app state.
static OSStatus ConsumersChanged(CMIOObjectID, UInt32, const CMIOObjectPropertyAddress[], void *) { return noErr; }
// A non-null callback establishes the output queue. Passing nullptr unregisters
// the queue and can succeed while returning a null CMSimpleQueueRef.
// We poll capacity on the main thread, so no work is needed on removal.
static void SinkQueueChanged(CMIOStreamID, void *, void *) {}

@implementation CLVirtualCameraClient {
    CMIODeviceID _device;
    CMIOStreamID _sink;
    CMSimpleQueueRef _queue;
    NSInteger _consumerCount;
    NSString *_status;
    CFAbsoluteTime _retryAfter;
    BOOL _started;
    BOOL _deliveryAvailable;
}
- (instancetype)init {
    if ((self = [super init])) { _status = @"Choose Camera setup to install or update Callayers."; }
    return self;
}
- (NSInteger)consumerCount { return _consumerCount; }
- (NSString *)status { return _status; }
- (NSString *)deliveryState { return _device && _deliveryAvailable ? StringProperty(_device, 'cliv') : nil; }
- (void)refresh {
    // Modern camera extensions are in the normal device list. Do not rewrite
    // the legacy screen-capture flag: it can stall discovery after camera use.
    CMIODeviceID found = 0;
    for (auto device : Objects(kCMIOObjectSystemObject, Address(kCMIOHardwarePropertyDevices))) {
        if ([StringProperty(device, kCMIODevicePropertyDeviceUID) isEqualToString:@"com.callayers.virtual-camera"]) { found = device; break; }
    }

    if (!found || (_device && _device != found)) {
        [self stop];
        auto countAddress = Address('cact');
        if (_device) {
            CMIOObjectRemovePropertyListener(_device, &countAddress, ConsumersChanged, nullptr);
            auto deliveryAddress = Address('cliv');
            if (_deliveryAvailable) CMIOObjectRemovePropertyListener(_device, &deliveryAddress, ConsumersChanged, nullptr);
        }
        _device = 0; _sink = 0; _deliveryAvailable = NO;
    }
    _consumerCount = 0;
    if (!found) { _status = @"Callayers camera is not active. Open Camera setup."; return; }
    if (_device != found) {
        _device = found;
        auto countAddress = Address('cact');
        CMIOObjectAddPropertyListener(_device, &countAddress, ConsumersChanged, nullptr);
        auto deliveryAddress = Address('cliv');
        _deliveryAvailable = CMIOObjectHasProperty(_device, &deliveryAddress);
        if (_deliveryAvailable) CMIOObjectAddPropertyListener(_device, &deliveryAddress, ConsumersChanged, nullptr);
    }
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
    else if (_queue) { _status = @"Sending workspace to Callayers camera."; }
    else if (CFAbsoluteTimeGetCurrent() >= _retryAfter) { _status = @"Connecting to Callayers camera…"; }
}
- (BOOL)sendSample:(CMSampleBufferRef)sample {
    if (!_consumerCount || !_sink) return NO;
    if (!_queue) {
        if (CFAbsoluteTimeGetCurrent() < _retryAfter) return NO;
        OSStatus result = CMIOStreamCopyBufferQueue(_sink, SinkQueueChanged, nullptr, &_queue);
        if (result == noErr && !_queue) result = kCMIOHardwareUnspecifiedError;
        if (result == noErr) result = CMIODeviceStartStream(_device, _sink);
        if (result != noErr) {
            [self stop]; _retryAfter = CFAbsoluteTimeGetCurrent() + 3;
            _status = [NSString stringWithFormat:@"Could not connect to Callayers camera (%d). Retrying…", (int)result]; return NO;
        }
        _started = YES;
    }
    if (CMSimpleQueueGetCount(_queue) >= MIN(2, CMSimpleQueueGetCapacity(_queue))) return NO;
    CFRetain(sample);
    const OSStatus result = CMSimpleQueueEnqueue(_queue, sample);
    if (result != noErr) { CFRelease(sample); return NO; }
    _status = @"Sending workspace to Callayers camera.";
    return YES;
}
- (void)stop {
    if (!_queue) return;
    if (_started) CMIODeviceStopStream(_device, _sink);
    _started = NO;
    while (const void *sample = CMSimpleQueueDequeue(_queue)) CFRelease(sample);
    CFRelease(_queue); _queue = nullptr;
}
- (void)disconnect {
    [self stop];
    auto address = Address('cact');
    if (_device) {
        CMIOObjectRemovePropertyListener(_device, &address, ConsumersChanged, nullptr);
        auto deliveryAddress = Address('cliv');
        if (_deliveryAvailable) CMIOObjectRemovePropertyListener(_device, &deliveryAddress, ConsumersChanged, nullptr);
    }
    _device = 0; _sink = 0; _consumerCount = 0; _deliveryAvailable = NO;
}
- (void)dealloc { [self disconnect]; }
@end
