#import <Foundation/Foundation.h>
#import <CoreMedia/CoreMedia.h>

NS_ASSUME_NONNULL_BEGIN
// Main-thread API. CoreMediaIO owns consumption on the other end of its queue.
@interface CLVirtualCameraClient : NSObject
@property(nonatomic, readonly) NSInteger consumerCount;
@property(nonatomic, readonly) NSString *status;
@property(nonatomic, readonly) NSString *consumerApp;
@property(nonatomic, readonly) int64_t deliveredTimestamp;
- (void)refresh;
- (BOOL)sendSample:(CMSampleBufferRef)sample;
- (void)stop;
@end
NS_ASSUME_NONNULL_END
