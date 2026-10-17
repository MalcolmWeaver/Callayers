import AVFoundation
import VideoToolbox

// Session changes and delivery run on one queue. The UI reads only the latest
// image through a lock, so a slow UI cannot accumulate camera buffers.
final class CameraCapture: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate, @unchecked Sendable {
    static func devices() -> [AVCaptureDevice] {
        let types: [AVCaptureDevice.DeviceType]
        if #available(macOS 14, *) { types = [.builtInWideAngleCamera, .external, .continuityCamera] }
        else { types = [.builtInWideAngleCamera, .externalUnknown] }
        return AVCaptureDevice.DiscoverySession(deviceTypes: types, mediaType: .video, position: .unspecified).devices
            .filter { $0.uniqueID != "com.callayers.virtual-camera" && $0.localizedName != "Callayers" }
    }
    private let queue = DispatchQueue(label: "com.callayers.capture", qos: .userInitiated, autoreleaseFrequency: .workItem)
    private let session = AVCaptureSession()
    private let output = AVCaptureVideoDataOutput()
    private let lock = NSLock()
    private var image: CGImage?
    private var imageTime: TimeInterval = 0
    private var problem = ""
    private var currentID: String?
    private var desiredID: String?
    private var errorObserver: NSObjectProtocol?

    override init() {
        super.init()
        output.alwaysDiscardsLateVideoFrames = true
        output.videoSettings = [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA]
        output.setSampleBufferDelegate(self, queue: queue)
        errorObserver = NotificationCenter.default.addObserver(forName: .AVCaptureSessionRuntimeError, object: session, queue: nil) { [weak self] note in
            let detail = (note.userInfo?[AVCaptureSessionErrorKey] as? Error)?.localizedDescription ?? "Camera stopped. Choose Retry camera."
            self?.queue.async { [weak self] in self?.clear(detail) }
        }
    }
    deinit { if let errorObserver { NotificationCenter.default.removeObserver(errorObserver) } }

    func configure(id: String?, retry: Bool = false) {
        queue.async { [weak self] in self?.apply(id: id, retry: retry) }
    }
    private func clear(_ message: String = "") {
        lock.lock(); image = nil; imageTime = 0; problem = message; lock.unlock()
    }
    private func apply(id: String?, retry: Bool) {
        guard retry || desiredID != id else { return }
        desiredID = id
        if session.isRunning { session.stopRunning() }
        clear()
        currentID = nil
        session.beginConfiguration()
        for input in session.inputs { session.removeInput(input) }
        if session.outputs.contains(output) { session.removeOutput(output) }
        guard let id else { session.commitConfiguration(); return }
        guard AVCaptureDevice.authorizationStatus(for: .video) == .authorized else {
            session.commitConfiguration(); clear("Allow camera access in System Settings → Privacy & Security → Camera."); return
        }
        guard let camera = Self.devices().first(where: { $0.uniqueID == id }) else {
            session.commitConfiguration(); clear("The selected webcam is unavailable. Reconnect it and choose Retry camera."); return
        }
        do {
            let input = try AVCaptureDeviceInput(device: camera)
            guard session.canAddInput(input), session.canAddOutput(output) else { throw FrameComposer.error("Camera configuration", -1) }
            session.addInput(input); session.addOutput(output)
            if session.canSetSessionPreset(.hd1280x720) { session.sessionPreset = .hd1280x720 }
            else if session.canSetSessionPreset(.high) { session.sessionPreset = .high }
            if camera.activeFormat.videoSupportedFrameRateRanges.contains(where: { $0.minFrameRate <= 30 && $0.maxFrameRate >= 30 }) {
                try camera.lockForConfiguration()
                camera.activeVideoMinFrameDuration = CMTime(value: 1, timescale: 30)
                camera.activeVideoMaxFrameDuration = CMTime(value: 1, timescale: 30)
                camera.unlockForConfiguration()
            }
            if let connection = output.connection(with: .video), connection.isVideoMirroringSupported {
                connection.automaticallyAdjustsVideoMirroring = false
                connection.isVideoMirrored = false
            }
            session.commitConfiguration()
            currentID = id
            session.startRunning()
            if !session.isRunning { clear("The webcam could not start. Close other camera apps and choose Retry camera.") }
        } catch {
            session.commitConfiguration()
            clear("Could not start webcam: \(error.localizedDescription)")
        }
    }
    func latest() -> (CGImage?, String) {
        lock.lock(); defer { lock.unlock() }
        if image != nil && ProcessInfo.processInfo.systemUptime - imageTime > 2 {
            return (nil, "No new webcam frames. Reconnect the camera and choose Retry camera.")
        }
        return (image, problem)
    }
    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
        guard currentID != nil, let pixels = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
        var frame: CGImage?
        guard VTCreateCGImageFromCVPixelBuffer(pixels, options: nil, imageOut: &frame) == noErr, let frame else { return }
        lock.lock(); image = frame; imageTime = ProcessInfo.processInfo.systemUptime; problem = ""; lock.unlock()
    }
}
