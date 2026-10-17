#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
// Debug app mode: only the virtual camera is opened, never a physical webcam.
#ifdef __cplusplus
extern "C" {
#endif
NSDictionary *CLRunCameraTransportChecks(double duration);
#ifdef __cplusplus
}
#endif
NS_ASSUME_NONNULL_END
