#import <Foundation/Foundation.h>
#import <CoreMedia/CoreMedia.h>
NS_ASSUME_NONNULL_BEGIN
// The producer owns its sample until the next prepare call. Main-thread only.
@protocol CLCameraTestProducer <NSObject>
- (BOOL)prepareFrame:(NSUInteger)sequence;
@property(nonatomic, readonly, nullable) CMSampleBufferRef sample;
@property(nonatomic, readonly) NSString *status;
- (void)restart;
- (void)stop;
@end
#ifdef __cplusplus
extern "C" {
#endif
// A nil producer uses synthetic frames and never opens a physical webcam.
NSDictionary *CLRunCameraTransportChecks(double duration, id<CLCameraTestProducer> _Nullable producer);
#ifdef __cplusplus
}
#endif
NS_ASSUME_NONNULL_END
