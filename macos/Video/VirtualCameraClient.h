#import <Foundation/Foundation.h>
#import <CoreMedia/CoreMedia.h>

NS_ASSUME_NONNULL_BEGIN
// Main-thread API. CoreMediaIO owns consumption on the other end of its queue.
@interface CLVirtualCameraClient : NSObject
@property(nonatomic, readonly) NSInteger consumerCount;
@property(nonatomic, readonly) NSString *status;
- (void)refresh;
- (BOOL)sendSample:(CMSampleBufferRef)sample;
- (void)stop;
@end
NS_ASSUME_NONNULL_END
