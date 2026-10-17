import AVFoundation
import CoreGraphics

// Uses production capture and composition without touching the saved workspace.
final class WebcamTestProducer: NSObject, CLCameraTestProducer {
    private let capture = CameraCapture()
    private let composer: FrameComposer
    private let cameraID: String
    private var lastArrival: TimeInterval = 0
    private(set) var sample: CMSampleBuffer?
    private(set) var status = "Opening webcam"

    init(cameraID: String) throws {
        composer = try FrameComposer()
        self.cameraID = cameraID
        super.init()
        capture.configure(id: cameraID, retry: true)
    }
    func prepareFrame(_ sequence: UInt) -> Bool {
        sample = nil
        let (image, arrival, problem) = capture.latestFrame()
        guard let image, arrival > lastArrival else {
            status = problem.isEmpty ? "Waiting for a fresh webcam frame" : problem
            return false
        }
        lastArrival = arrival
        // Encode a sequence into a small opaque overlay. The consumer checks
        // these pixels AND webcam pixels against the exact composed frame.
        var bytes = [UInt8](repeating: 255, count: 20 * 20 * 4)
        for y in 0..<20 {
            for x in 0..<20 {
                let offset = (y * 20 + x) * 4
                if x < 2 {
                    bytes[offset] = 31; bytes[offset + 1] = 173; bytes[offset + 2] = 220 + UInt8(sequence % 4)
                } else {
                    let nibble = (sequence >> (UInt((x - 2) / 2) * 4)) & 15
                    bytes[offset] = 16 + UInt8(nibble) * 8
                    bytes[offset + 1] = 73; bytes[offset + 2] = 133
                }
            }
        }
        guard let provider = CGDataProvider(data: Data(bytes) as CFData),
              let marker = CGImage(width: 20, height: 20, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: 80,
                space: CGColorSpace(name: CGColorSpace.sRGB)!,
                bitmapInfo: CGBitmapInfo(rawValue: CGBitmapInfo.byteOrder32Little.rawValue | CGImageAlphaInfo.premultipliedFirst.rawValue),
                provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent) else { return false }
        do {
            sample = try composer.compose(camera: image,
                layers: [VideoLayer(image: marker, rect: CGRect(x: 0, y: 0, width: 40, height: 40))],
                at: CMClockGetTime(CMClockGetHostTimeClock()))?.0
            status = sample == nil ? "Composition buffer busy" : "Fresh webcam composed"
            return sample != nil
        } catch { status = error.localizedDescription; return false }
    }
    func restart() {
        sample = nil; lastArrival = 0
        capture.configure(id: cameraID, retry: true)
    }
    func stop() {
        sample = nil
        capture.configure(id: nil, retry: true)
    }
}
