import AppKit
import AVFoundation
import Combine

struct WebcamChoice: Identifiable {
    let id: String
    let name: String
}

@MainActor
final class VideoModel: ObservableObject {
    @Published private(set) var cameras: [WebcamChoice] = []
    @Published private(set) var selectedID: String
    @Published private(set) var cameraStatus = "Choose your webcam."
    @Published private(set) var outputStatus = ""
    private(set) var preview: NSImage?
    private(set) var wantsFrames = true
    private let capture = CameraCapture()
    private let output = CLVirtualCameraClient()
    private var composer: FrameComposer?
    private var retainedSample: CMSampleBuffer?
    private var requestedID: String?
    private var refreshTime = Date.distantPast
    private var terminated = false
    private var permissionPending = false
    private static let preference = "selectedWebcamID"

    init() {
        selectedID = UserDefaults.standard.string(forKey: Self.preference) ?? ""
        do { composer = try FrameComposer() }
        catch { cameraStatus = error.localizedDescription }
        refresh()
    }
    func choose(_ id: String) {
        selectedID = id
        UserDefaults.standard.set(id, forKey: Self.preference)
        // Immediately clear the old picture when changing devices.
        preview = nil; retainedSample = nil
        capture.configure(id: nil, retry: true); requestedID = nil
        retry()
    }
    func retry() {
        if !selectedID.isEmpty, AVCaptureDevice.authorizationStatus(for: .video) == .notDetermined {
            guard !permissionPending else { return }
            permissionPending = true
            AVCaptureDevice.requestAccess(for: .video) { [weak self] _ in
                Task { @MainActor in self?.permissionPending = false; self?.refresh(force: true) }
            }
        } else { refresh(force: true) }
    }
    private func refresh(force: Bool = false) {
        guard !terminated else { return }
        refreshTime = Date().addingTimeInterval(1)
        cameras = CameraCapture.devices().map { WebcamChoice(id: $0.uniqueID, name: $0.localizedName) }
        output.refresh(); outputStatus = output.status
        let visible = NSApp?.windows.contains { $0.isVisible && !$0.isMiniaturized && $0.occlusionState.contains(.visible) } ?? true
        wantsFrames = visible || output.consumerCount > 0
        let exists = cameras.contains { $0.id == selectedID }
        let id = wantsFrames && exists && !selectedID.isEmpty ? selectedID : nil
        if id != requestedID || force {
            requestedID = id
            capture.configure(id: id, retry: force)
        }
        if selectedID.isEmpty { cameraStatus = "Choose your webcam." }
        else if !exists { cameraStatus = "Saved webcam unavailable. Reconnect it or choose another." }
        else if !wantsFrames { cameraStatus = "Camera paused while the preview and output are idle." }
        else if AVCaptureDevice.authorizationStatus(for: .video) != .authorized {
            cameraStatus = "Allow webcam access, then choose Retry camera."
        } else {
            let (image, error) = capture.latest()
            cameraStatus = error.isEmpty ? (image == nil ? "Opening webcam…" : "Webcam live") : error
        }
    }
    func tick(layers: [VideoLayer]) -> Bool {
        guard !terminated else { return false }
        if Date() >= refreshTime { refresh() }
        guard wantsFrames, let composer else { return false }
        let camera = capture.latest().0
        do {
            guard let (sample, image) = try composer.compose(camera: camera, layers: layers, at: CMClockGetTime(CMClockGetHostTimeClock())) else { return false }
            retainedSample = sample
            preview = NSImage(cgImage: image, size: NSSize(width: 1280, height: 720))
            if output.consumerCount > 0 {
                _ = output.sendSample(sample)
                if outputStatus != output.status { outputStatus = output.status }
            }
            return true
        } catch { cameraStatus = error.localizedDescription; return false }
    }
    func shutdown() {
        terminated = true; output.stop(); capture.configure(id: nil, retry: true)
        retainedSample = nil; preview = nil
    }
}
