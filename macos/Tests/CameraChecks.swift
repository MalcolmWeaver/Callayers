import Foundation
import CoreMedia
import CoreVideo
import CoreGraphics
import ImageIO
import UniformTypeIdentifiers

@main
struct CameraChecks {
    static func require(_ condition: @autoclosure () -> Bool, _ message: String) throws {
        if !condition() { throw NSError(domain: "CameraChecks", code: 1, userInfo: [NSLocalizedDescriptionKey: message]) }
    }
    static func bytes(_ sample: CMSampleBuffer) throws -> Data {
        guard let buffer = CMSampleBufferGetImageBuffer(sample) else { throw CameraError.setup("Missing image", -1) }
        CVPixelBufferLockBaseAddress(buffer, .readOnly)
        defer { CVPixelBufferUnlockBaseAddress(buffer, .readOnly) }
        return Data(bytes: CVPixelBufferGetBaseAddress(buffer)!, count: CVPixelBufferGetBytesPerRow(buffer) * CVPixelBufferGetHeight(buffer))
    }
    static func main() throws {
        let picture = try TestPicture()
        let firstTime = CMTime(value: 12, timescale: 1)
        let first = try picture.sample(at: firstTime)!
        let second = try picture.sample(at: CMTime(value: 14, timescale: 1))!
        let firstBytes = try bytes(first), secondBytes = try bytes(second)
        let image = CMSampleBufferGetImageBuffer(first)!
        try require(CVPixelBufferGetWidth(image) == 1920 && CVPixelBufferGetHeight(image) == 1080, "Wrong dimensions")
        try require(CVPixelBufferGetPixelFormatType(image) == kCVPixelFormatType_32BGRA, "Wrong format")
        try require(CMSampleBufferGetPresentationTimeStamp(first) == firstTime, "Host timestamp was lost")
        try require(CMSampleBufferGetDuration(first) == CMTime(value: 1, timescale: 30), "Wrong duration")
        let rowBytes = CVPixelBufferGetBytesPerRow(image)
        for y in 0..<1080 {
            for x in 0..<1920 { try require(firstBytes[y * rowBytes + x * 4 + 3] == 255, "Camera picture contains transparent pixels") }
        }
        try require(firstBytes != secondBytes, "Picture did not animate")
        // At 12 seconds the stripe is at the left; at 14 seconds it is halfway across.
        if CommandLine.arguments.count > 1 {
            let provider = CGDataProvider(data: firstBytes as CFData)!
            let cgImage = CGImage(width: 1920, height: 1080, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: rowBytes,
                space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGBitmapInfo(rawValue: CGBitmapInfo.byteOrder32Little.rawValue | CGImageAlphaInfo.premultipliedFirst.rawValue),
                provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)!
            let destination = CGImageDestinationCreateWithURL(URL(fileURLWithPath: CommandLine.arguments[1]) as CFURL, UTType.png.identifier as CFString, 1, nil)!
            CGImageDestinationAddImage(destination, cgImage, nil)
            try require(CGImageDestinationFinalize(destination), "Could not write picture preview")
        }
        let stripePixel = 840 * rowBytes + 150 * 4
        try require(firstBytes[stripePixel] > 240 && secondBytes[stripePixel] < 80, "Moving stripe has wrong position or orientation")
        print("PASS opaque animated 1080p BGRA frames, host timestamps and 30 FPS duration")

        let bounded = try TestPicture()
        var held: [CMSampleBuffer] = []
        for _ in 0..<5 { held.append(try bounded.sample(at: firstTime)!) }
        let overflow = try bounded.sample(at: firstTime)
        try require(overflow == nil, "Slow consumer exceeded the five-buffer limit")
        held.removeAll()
        let recovered = try bounded.sample(at: firstTime)
        try require(recovered != nil, "Frame generation did not recover after consumer release")
        print("PASS slow consumers drop frames at the buffer limit and recover when buffers are released")


    }
}
