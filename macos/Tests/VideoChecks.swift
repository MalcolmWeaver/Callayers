import Foundation
import CoreMedia
import CoreVideo
import CoreGraphics

@main
struct VideoChecks {
    static func color(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat, _ a: CGFloat) -> CGColor {
        CGColor(colorSpace: CGColorSpace(name: CGColorSpace.sRGB)!, components: [r, g, b, a])!
    }
    static func require(_ value: @autoclosure () -> Bool, _ message: String) throws {
        if !value() { throw FrameComposer.error(message, -1) }
    }
    static func image(width: Int, height: Int, draw: (CGContext) -> Void) -> CGImage {
        let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
            space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGBitmapInfo.byteOrder32Little.rawValue | CGImageAlphaInfo.premultipliedFirst.rawValue)!
        draw(context)
        return context.makeImage()!
    }
    static func pixel(_ sample: CMSampleBuffer, x: Int, y: Int) -> [UInt8] {
        let image = CMSampleBufferGetImageBuffer(sample)!
        CVPixelBufferLockBaseAddress(image, .readOnly)
        defer { CVPixelBufferUnlockBaseAddress(image, .readOnly) }
        let address = CVPixelBufferGetBaseAddress(image)!.assumingMemoryBound(to: UInt8.self)
        let offset = y * CVPixelBufferGetBytesPerRow(image) + x * 4
        return Array(UnsafeBufferPointer(start: address + offset, count: 4))
    }
    static func main() throws {
        let camera = image(width: 1280, height: 720) { c in
            c.setFillColor(color(1, 0, 0, 1)); c.fill(CGRect(x: 0, y: 360, width: 1280, height: 360))
            c.setFillColor(color(0, 1, 0, 1)); c.fill(CGRect(x: 0, y: 0, width: 1280, height: 360))
        }
        let overlay = image(width: 200, height: 100) { c in
            c.setFillColor(color(0, 0, 1, 0.5)); c.fill(CGRect(x: 0, y: 0, width: 200, height: 100))
        }
        let composer = try FrameComposer()
        let time = CMTime(value: 20, timescale: 1)
        let layer = VideoLayer(image: overlay, rect: CGRect(x: 100, y: 100, width: 200, height: 100))
        let (sample, _) = try composer.compose(camera: camera, layers: [layer], at: time)!
        try require(pixel(sample, x: 20, y: 20) == [0, 0, 255, 255], "Webcam top is wrong: \(pixel(sample, x: 20, y: 20))")
        try require(pixel(sample, x: 20, y: 1000) == [0, 255, 0, 255], "Webcam bottom is wrong: \(pixel(sample, x: 20, y: 1000))")
        let blended = pixel(sample, x: 200, y: 200)
        try require(abs(Int(blended[0]) - 128) <= 1 && abs(Int(blended[2]) - 127) <= 1 && blended[3] == 255, "Transparent card was misplaced or did not blend")
        try require(pixel(sample, x: 148, y: 200) == [0, 0, 255, 255], "Private selection border leaked into output")
        let (hidden, _) = try composer.compose(camera: camera, layers: [], at: time)!
        try require(pixel(hidden, x: 200, y: 200) == [0, 0, 255, 255], "Hidden card remained in output")
        try require(CMSampleBufferGetPresentationTimeStamp(sample) == time, "Composition changed timestamp")
        print("PASS webcam orientation, transparent card placement, hiding and private-border exclusion")

        let mailbox = LatestVideoFrame()
        try require(mailbox.accept(sample, now: time), "Fresh composed frame was rejected")
        try require(mailbox.sample(now: CMTime(value: 205, timescale: 10)) != nil, "Live frame expired early")
        try require(mailbox.sample(now: CMTime(value: 22, timescale: 1)) == nil, "Stalled app left a frozen webcam frame")
        try require(!mailbox.accept(sample, now: CMTime(value: 22, timescale: 1)), "Stale queued frame was accepted")
        try require(!mailbox.accept(sample, now: CMTime(value: 10, timescale: 1)), "Future timestamp was accepted")
        mailbox.accept(sample, now: time); mailbox.clear()
        try require(mailbox.sample(now: time) == nil, "Disconnected producer retained its frame")
        print("PASS stale/future rejection and immediate disconnect clearing")
    }
}
