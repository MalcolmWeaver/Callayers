#import "RendererBridge.h"
#include "Renderer.h"
#include <memory>
#include <stdexcept>

@implementation CLRendererBridge {
    std::unique_ptr<callayers::mac::Renderer> _renderer;
    NSString *_bridgeProblem;
    NSImage *_image;
}
- (instancetype)initWithResources:(NSURL *)resources {
    self = [super init];
    if (self) {
        _bridgeProblem = @"";
        try { _renderer = std::make_unique<callayers::mac::Renderer>(resources.path.UTF8String); }
        catch (const std::exception& error) { _bridgeProblem = @(error.what()); }
    }
    return self;
}
- (NSString *)problem {
    if (_bridgeProblem.length) return _bridgeProblem;
    return _renderer ? @(_renderer->problem().c_str()) : @"The card renderer could not start.";
}
- (BOOL)ready { return _renderer && _renderer->ready() && !self.problem.length; }
- (NSImage *)image { return _image; }
- (BOOL)loadCard:(NSString *)html state:(NSString *)state {
    if (!_renderer) return NO;
    try { _bridgeProblem = @""; _renderer->loadCard(html.UTF8String, state.UTF8String); return YES; }
    catch (const std::exception& error) { _bridgeProblem = @(error.what()); return NO; }
}
- (void)clear {
    if (!_renderer) return;
    try { _renderer->clear(); _image = nil; }
    catch (const std::exception& error) { _bridgeProblem = @(error.what()); }
}
- (BOOL)tick {
    if (!_renderer) return NO;
    try {
        if (!_renderer->tick()) return NO;
        const auto& frame = _renderer->frame();
        if (frame.bgra.empty()) return NO;
        NSData *data = [NSData dataWithBytes:frame.bgra.data() length:frame.bgra.size()];
        CGDataProviderRef provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
        CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        if (!provider || !colorSpace) {
            if (provider) CGDataProviderRelease(provider);
            if (colorSpace) CGColorSpaceRelease(colorSpace);
            throw std::runtime_error("Could not display the rendered card");
        }
        CGImageRef image = CGImageCreate(frame.width, frame.height, 8, 32, frame.rowBytes, colorSpace,
            kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst, provider, nullptr, true, kCGRenderingIntentDefault);
        CGDataProviderRelease(provider); CGColorSpaceRelease(colorSpace);
        if (!image) throw std::runtime_error("Could not display the rendered card");
        _image = [[NSImage alloc] initWithCGImage:image size:NSMakeSize(1280, 720)];
        CGImageRelease(image);
        return YES;
    } catch (const std::exception& error) { _bridgeProblem = @(error.what()); return NO; }
}
- (void)setCardScale:(double)value {
    if (!_renderer) return;
    try { _renderer->scale(value); }
    catch (const std::exception& error) { _bridgeProblem = @(error.what()); }
}
- (NSUInteger)beginSnapshot {
    if (!_renderer) return 0;
    try { return _renderer->requestSnapshot(); }
    catch (const std::exception& error) { _bridgeProblem = @(error.what()); return 0; }
}
- (NSString *)snapshotForRequest:(NSUInteger)request {
    if (!_renderer) return nil;
    try {
        auto result = _renderer->snapshot(static_cast<unsigned>(request));
        return result.empty() ? nil : @(result.c_str());
    } catch (const std::exception& error) { _bridgeProblem = @(error.what()); return nil; }
}
- (void)mouse:(NSInteger)type x:(NSInteger)x y:(NSInteger)y button:(NSInteger)button {
    if (!_renderer) return;
    try { _renderer->mouse(static_cast<int>(type), static_cast<int>(x), static_cast<int>(y), static_cast<int>(button)); }
    catch (const std::exception& error) { _bridgeProblem = @(error.what()); }
}
- (void)scrollAtX:(NSInteger)x y:(NSInteger)y deltaX:(NSInteger)dx deltaY:(NSInteger)dy {
    if (!_renderer) return;
    try { _renderer->scroll(static_cast<int>(x), static_cast<int>(y), static_cast<int>(dx), static_cast<int>(dy)); }
    catch (const std::exception& error) { _bridgeProblem = @(error.what()); }
}
- (void)keyEvent:(NSEvent *)event {
    if (!_renderer || (event.type != NSEventTypeKeyDown && event.type != NSEventTypeKeyUp)) return;
    try {
        const NSEventModifierFlags flags = event.modifierFlags;
        const unsigned modifiers = ((flags & NSEventModifierFlagOption) ? 1 : 0) |
            ((flags & NSEventModifierFlagControl) ? 2 : 0) |
            ((flags & NSEventModifierFlagCommand) ? 4 : 0) |
            ((flags & NSEventModifierFlagShift) ? 8 : 0);
        int key = 0;
        switch (event.keyCode) {
            case 36: case 76: key = 13; break;
            case 48: key = 9; break; case 51: key = 8; break;
            case 53: key = 27; break; case 117: key = 46; break;
            case 123: key = 37; break; case 124: key = 39; break;
            case 125: key = 40; break; case 126: key = 38; break;
            case 115: key = 36; break; case 119: key = 35; break;
            default: {
                NSString *characters = event.charactersIgnoringModifiers.uppercaseString;
                if (characters.length) key = [characters characterAtIndex:0];
            }
        }
        const auto text = std::string((event.characters ?: @"").UTF8String);
        const auto unmodified = std::string((event.charactersIgnoringModifiers ?: @"").UTF8String);
        _renderer->key(event.type == NSEventTypeKeyDown ? 2 : 1, key, event.keyCode, modifiers, "", unmodified, event.isARepeat);
        if (event.type == NSEventTypeKeyDown && !(flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) &&
            !text.empty() && key != 8 && key != 9 && key != 27 && key != 46 && !(key >= 35 && key <= 40))
            _renderer->key(3, key, event.keyCode, modifiers, text, unmodified, event.isARepeat);
    } catch (const std::exception& error) { _bridgeProblem = @(error.what()); }
}
- (void)blur {
    if (!_renderer) return;
    try { _renderer->blur(); }
    catch (const std::exception& error) { _bridgeProblem = @(error.what()); }
}
@end
