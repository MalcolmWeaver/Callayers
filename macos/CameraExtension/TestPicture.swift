import Foundation
import CoreMedia
import CoreVideo
import CoreGraphics
import CoreText

enum CameraError: Error {
    case setup(String, OSStatus)
    case unsupportedFormat
}

// The first camera checkpoint generates frames inside the extension. App-to-
// extension transport and webcam composition are the next, separate increment.
final class TestPicture {
    static let width = 1920
    static let height = 1080
    static let frameDuration = CMTime(value: 1, timescale: 30)
    let format: CMVideoFormatDescription
    private let pool: CVPixelBufferPool
    private let allocationLimit = [kCVPixelBufferPoolAllocationThresholdKey: 5] as CFDictionary
    private let offline: Bool

    init(offline: Bool = false) throws {
        self.offline = offline
        var description: CMVideoFormatDescription?
        let status = CMVideoFormatDescriptionCreate(allocator: kCFAllocatorDefault,
            codecType: kCVPixelFormatType_32BGRA, width: Int32(Self.width), height: Int32(Self.height),
            extensions: nil, formatDescriptionOut: &description)
        guard status == noErr, let description else { throw CameraError.setup("Video format", status) }
        format = description
        var createdPool: CVPixelBufferPool?
        let attributes: [CFString: Any] = [
            kCVPixelBufferWidthKey: Self.width, kCVPixelBufferHeightKey: Self.height,
            kCVPixelBufferPixelFormatTypeKey: kCVPixelFormatType_32BGRA,
            kCVPixelBufferIOSurfacePropertiesKey: [:] as [String: Any],
            kCVPixelBufferCGImageCompatibilityKey: true,
            kCVPixelBufferCGBitmapContextCompatibilityKey: true
        ]
        let poolStatus = CVPixelBufferPoolCreate(kCFAllocatorDefault, nil, attributes as CFDictionary, &createdPool)
        guard poolStatus == kCVReturnSuccess, let createdPool else { throw CameraError.setup("Pixel pool", poolStatus) }
        pool = createdPool
    }

    // A slow consumer cannot create an unbounded queue: drop this tick when all
    // five buffers are retained downstream, then try again at the next tick.
    func sample(at time: CMTime) throws -> CMSampleBuffer? {
        var buffer: CVPixelBuffer?
        let status = CVPixelBufferPoolCreatePixelBufferWithAuxAttributes(kCFAllocatorDefault, pool, allocationLimit, &buffer)
        if status == kCVReturnWouldExceedAllocationThreshold { return nil }
        guard status == kCVReturnSuccess, let buffer else { throw CameraError.setup("Pixel buffer", status) }
        try draw(into: buffer, time: time)
        var timing = CMSampleTimingInfo(duration: Self.frameDuration, presentationTimeStamp: time, decodeTimeStamp: .invalid)
        var sample: CMSampleBuffer?
        let sampleStatus = CMSampleBufferCreateReadyWithImageBuffer(allocator: kCFAllocatorDefault,
            imageBuffer: buffer, formatDescription: format, sampleTiming: &timing, sampleBufferOut: &sample)
        guard sampleStatus == noErr, let sample else { throw CameraError.setup("Sample buffer", sampleStatus) }
        return sample
    }

    private func draw(into buffer: CVPixelBuffer, time: CMTime) throws {
        let lockStatus = CVPixelBufferLockBaseAddress(buffer, [])
        guard lockStatus == kCVReturnSuccess else { throw CameraError.setup("Pixel lock", lockStatus) }
        defer { CVPixelBufferUnlockBaseAddress(buffer, []) }
        guard let context = CGContext(data: CVPixelBufferGetBaseAddress(buffer), width: Self.width, height: Self.height,
            bitsPerComponent: 8, bytesPerRow: CVPixelBufferGetBytesPerRow(buffer), space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo.byteOrder32Little.rawValue | CGImageAlphaInfo.premultipliedFirst.rawValue)
        else { throw CameraError.setup("Drawing context", -1) }
        context.setFillColor(CGColor(red: 0.035, green: 0.065, blue: 0.11, alpha: 1))
        context.fill(CGRect(x: 0, y: 0, width: Self.width, height: Self.height))
        if offline {
            text("CALLAYERS", at: CGPoint(x: 100, y: 760), size: 88, in: context)
            text("Camera unavailable", at: CGPoint(x: 100, y: 610), size: 48, in: context)
            text("Open Callayers and choose your webcam", at: CGPoint(x: 100, y: 510), size: 40, in: context)
            return
        }
        // Quartz's bitmap context maps its bottom-left drawing coordinates
        // into the top-first pixel-buffer rows used by the video consumer.
        let colors: [CGColor] = [
            CGColor(red: 0.3, green: 0.85, blue: 0.73, alpha: 1),
            CGColor(red: 0.25, green: 0.55, blue: 0.9, alpha: 1),
            CGColor(red: 0.95, green: 0.65, blue: 0.3, alpha: 1)
        ]
        for (index, color) in colors.enumerated() {
            context.setFillColor(color)
            context.fill(CGRect(x: 100 + index * 580, y: 330, width: 560, height: 210))
        }
        text("CALLAYERS", at: CGPoint(x: 100, y: 860), size: 88, in: context)
        text("CAMERA TEST  ·  1920 × 1080  ·  30 FPS", at: CGPoint(x: 100, y: 770), size: 36, in: context)
        text("Moving bar = live video", at: CGPoint(x: 100, y: 630), size: 42, in: context)
        let seconds = max(0, time.seconds)
        let phase = seconds.truncatingRemainder(dividingBy: 4) / 4
        context.setFillColor(CGColor(gray: 0.18, alpha: 1))
        context.fill(CGRect(x: 100, y: 220, width: 1720, height: 36))
        context.setFillColor(CGColor(gray: 1, alpha: 1))
        context.fill(CGRect(x: 100 + phase * 1600, y: 220, width: 120, height: 36))
        text("Test picture only — webcam and cards come next", at: CGPoint(x: 100, y: 120), size: 34, in: context)
    }

    private func text(_ value: String, at point: CGPoint, size: CGFloat, in context: CGContext) {
        let attributes: [NSAttributedString.Key: Any] = [
            NSAttributedString.Key(kCTFontAttributeName as String): CTFontCreateWithName("Helvetica" as CFString, size, nil),
            NSAttributedString.Key(kCTForegroundColorAttributeName as String): CGColor(gray: 1, alpha: 1)
        ]
        context.textMatrix = .identity
        context.textPosition = point
        CTLineDraw(CTLineCreateWithAttributedString(NSAttributedString(string: value, attributes: attributes)), context)
    }
}
