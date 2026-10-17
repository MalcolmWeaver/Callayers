#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN
// All methods are called on the main thread. This development preview exposes
// card rendering only; it does not activate a camera or access account data.
@interface CLRendererBridge : NSObject
- (instancetype)initWithResources:(NSURL *)resources NS_SWIFT_NAME(init(resources:));
@property(nonatomic, readonly) BOOL ready;
@property(nonatomic, readonly) NSString *problem;
@property(nonatomic, readonly, nullable) NSImage *image;
- (BOOL)loadCard:(NSString *)html state:(NSString *)state;
- (void)clear;
- (BOOL)tick;
- (void)setCardScale:(double)value;
- (NSUInteger)beginSnapshot;
- (nullable NSString *)snapshotForRequest:(NSUInteger)request NS_SWIFT_NAME(snapshot(request:));
- (void)mouse:(NSInteger)type x:(NSInteger)x y:(NSInteger)y button:(NSInteger)button;
- (void)scrollAtX:(NSInteger)x y:(NSInteger)y deltaX:(NSInteger)deltaX deltaY:(NSInteger)deltaY NS_SWIFT_NAME(scroll(x:y:deltaX:deltaY:));
- (void)keyEvent:(NSEvent *)event;
- (void)blur;
@end
NS_ASSUME_NONNULL_END
