#import <Foundation/Foundation.h>
#import <CoreMediaIO/CoreMediaIO.h>
#include <vector>

// Integration probe: requires the installed camera extension and an idle sink.
// Sign copies with the real Callayers identity, a different identifier under the
// same team, and an ad-hoc Callayers identifier. Run with accept/reject/reject.
// This opens only the sink, sends no frames, and never reads a webcam image.
static std::vector<CMIOObjectID> Objects(CMIOObjectID object, UInt32 selector,
                                       UInt32 scope = kCMIOObjectPropertyScopeGlobal) {
    CMIOObjectPropertyAddress address = {selector, scope, kCMIOObjectPropertyElementMain};
    UInt32 size = 0, used = 0;
    if (CMIOObjectGetPropertyDataSize(object, &address, 0, nullptr, &size) || !size) return {};
    std::vector<CMIOObjectID> result(size / sizeof(CMIOObjectID));
    if (CMIOObjectGetPropertyData(object, &address, 0, nullptr, size, &used, result.data())) return {};
    result.resize(used / sizeof(CMIOObjectID));
    return result;
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc != 2 || (strcmp(argv[1], "accept") && strcmp(argv[1], "reject"))) {
            fprintf(stderr, "Usage: CameraIdentityChecks accept|reject\n");
            return 2;
        }
        const bool expectAccepted = !strcmp(argv[1], "accept");
        CMIOObjectPropertyAddress allow = {kCMIOHardwarePropertyAllowScreenCaptureDevices,
            kCMIOObjectPropertyScopeGlobal, kCMIOObjectPropertyElementMain};
        UInt32 yes = 1;
        CMIOObjectSetPropertyData(kCMIOObjectSystemObject, &allow, 0, nullptr, sizeof(yes), &yes);
        for (auto device : Objects(kCMIOObjectSystemObject, kCMIOHardwarePropertyDevices)) {
            CMIOObjectPropertyAddress uidAddress = {kCMIODevicePropertyDeviceUID,
                kCMIOObjectPropertyScopeGlobal, kCMIOObjectPropertyElementMain};
            CFStringRef uid = nullptr;
            UInt32 used = 0;
            if (CMIOObjectGetPropertyData(device, &uidAddress, 0, nullptr, sizeof(uid), &used, &uid)) continue;
            const bool match = uid && CFEqual(uid, CFSTR("com.callayers.virtual-camera"));
            if (uid) CFRelease(uid);
            if (!match) continue;
            for (auto sink : Objects(device, kCMIODevicePropertyStreams, kCMIODevicePropertyScopeOutput)) {
                const OSStatus status = CMIODeviceStartStream(device, sink);
                if (status == noErr) CMIODeviceStopStream(device, sink);
                // -4 is CMIO's refusal result. Missing devices and unrelated
                // transport failures must not count as successful rejection.
                const bool passed = expectAccepted ? status == noErr : status == -4;
                printf("%s producer %s (status %d)\n", passed ? "PASS" : "FAIL",
                       expectAccepted ? "accepted" : "rejected", (int)status);
                return passed ? 0 : 1;
            }
        }
        fprintf(stderr, "Callayers sink is not available; activation is required.\n");
        return 2;
    }
}
