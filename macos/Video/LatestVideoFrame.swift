import CoreMedia
import CoreVideo

// Accessed on the extension's serial queue. A single retained frame replaces
// any older one; a stalled or terminated app never leaves a frozen face live.
final class LatestVideoFrame {
    private var value: CMSampleBuffer?
    func clear() { value = nil }
    @discardableResult
    func accept(_ sample: CMSampleBuffer, now: CMTime) -> Bool {
        guard let image = CMSampleBufferGetImageBuffer(sample),
              CVPixelBufferGetWidth(image) == 1920, CVPixelBufferGetHeight(image) == 1080,
              CVPixelBufferGetPixelFormatType(image) == kCVPixelFormatType_32BGRA,
              fresh(sample, now: now) else { return false }
        value = sample
        return true
    }
    func sample(now: CMTime) -> CMSampleBuffer? {
        guard let value, fresh(value, now: now) else { self.value = nil; return nil }
        return value
    }
    private func fresh(_ sample: CMSampleBuffer, now: CMTime) -> Bool {
        let age = CMTimeSubtract(now, CMSampleBufferGetPresentationTimeStamp(sample)).seconds
        return age.isFinite && age >= -0.1 && age < 1
    }
}
