import Foundation
import CoreMedia
import CoreVideo
import CoreGraphics
import CoreText

struct VideoLayer {
    let image: CGImage
    // Logical top-left coordinates in the 1280 × 720 workspace.
    let rect: CGRect
}

final class FrameComposer {
    static let width = 1920
    static let height = 1080
    static let duration = CMTime(value: 1, timescale: 30)
    let format: CMVideoFormatDescription
    private let pool: CVPixelBufferPool

    init() throws {
        var format: CMVideoFormatDescription?
        let status = CMVideoFormatDescriptionCreate(allocator: kCFAllocatorDefault,
            codecType: kCVPixelFormatType_32BGRA, width: 1920, height: 1080,
            extensions: nil, formatDescriptionOut: &format)
        guard status == noErr, let format else { throw Self.error("Video format", status) }
        self.format = format
        var pool: CVPixelBufferPool?
        let attributes: [CFString: Any] = [kCVPixelBufferWidthKey: Self.width,
            kCVPixelBufferHeightKey: Self.height, kCVPixelBufferPixelFormatTypeKey: kCVPixelFormatType_32BGRA,
            kCVPixelBufferIOSurfacePropertiesKey: [:] as [String: Any],
            kCVPixelBufferCGBitmapContextCompatibilityKey: true]
        let result = CVPixelBufferPoolCreate(nil, nil, attributes as CFDictionary, &pool)
        guard result == kCVReturnSuccess, let pool else { throw Self.error("Video buffers", result) }
        self.pool = pool
    }

    static func error(_ operation: String, _ code: OSStatus) -> NSError {
        NSError(domain: "CallayersVideo", code: Int(code), userInfo: [NSLocalizedDescriptionKey: "\(operation) failed (\(code))."])
    }

    func compose(camera: CGImage?, layers: [VideoLayer], at time: CMTime) throws -> (CMSampleBuffer, CGImage)? {
        var buffer: CVPixelBuffer?
        let result = CVPixelBufferPoolCreatePixelBufferWithAuxAttributes(nil, pool,
            [kCVPixelBufferPoolAllocationThresholdKey: 5] as CFDictionary, &buffer)
        if result == kCVReturnWouldExceedAllocationThreshold { return nil }
        guard result == kCVReturnSuccess, let buffer else { throw Self.error("Video buffer", result) }
        let lock = CVPixelBufferLockBaseAddress(buffer, [])
        guard lock == kCVReturnSuccess else { throw Self.error("Video buffer lock", lock) }
        defer { CVPixelBufferUnlockBaseAddress(buffer, []) }
        guard let context = CGContext(data: CVPixelBufferGetBaseAddress(buffer), width: Self.width, height: Self.height,
            bitsPerComponent: 8, bytesPerRow: CVPixelBufferGetBytesPerRow(buffer), space: CGColorSpace(name: CGColorSpace.sRGB)!,
            bitmapInfo: CGBitmapInfo.byteOrder32Little.rawValue | CGImageAlphaInfo.premultipliedFirst.rawValue)
        else { throw Self.error("Video drawing", -1) }
        context.setFillColor(CGColor(red: 0.035, green: 0.065, blue: 0.11, alpha: 1))
        context.fill(CGRect(x: 0, y: 0, width: Self.width, height: Self.height))
        if let camera {
            // Fill the output without stretching; crop excess at the center.
            let scale = max(1920 / CGFloat(camera.width), 1080 / CGFloat(camera.height))
            let w = CGFloat(camera.width) * scale, h = CGFloat(camera.height) * scale
            context.draw(camera, in: CGRect(x: (1920 - w) / 2, y: (1080 - h) / 2, width: w, height: h))
        } else {
            let attributes: [NSAttributedString.Key: Any] = [
                NSAttributedString.Key(kCTFontAttributeName as String): CTFontCreateWithName("Helvetica" as CFString, 48, nil),
                NSAttributedString.Key(kCTForegroundColorAttributeName as String): CGColor(gray: 1, alpha: 1)]
            context.textPosition = CGPoint(x: 100, y: 920)
            CTLineDraw(CTLineCreateWithAttributedString(NSAttributedString(string: "Choose your webcam in Callayers", attributes: attributes)), context)
        }
        for layer in layers {
            let r = layer.rect
            context.draw(layer.image, in: CGRect(x: r.minX * 1.5, y: 1080 - r.maxY * 1.5,
                width: r.width * 1.5, height: r.height * 1.5))
        }
        guard let image = context.makeImage() else { throw Self.error("Video preview", -1) }
        var sample: CMSampleBuffer?
        var timing = CMSampleTimingInfo(duration: Self.duration, presentationTimeStamp: time, decodeTimeStamp: .invalid)
        let status = CMSampleBufferCreateReadyWithImageBuffer(allocator: nil, imageBuffer: buffer,
            formatDescription: format, sampleTiming: &timing, sampleBufferOut: &sample)
        guard status == noErr, let sample else { throw Self.error("Video sample", status) }
        return (sample, image)
    }
}
