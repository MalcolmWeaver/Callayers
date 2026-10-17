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
    private var sessionAllowed = false
    @Published private(set) var cameraReady = false
    @Published private(set) var consumerCount = 0
    private(set) var outputActive = false
    private var permissionPending = false
    private(set) var previewVisible = true
    private(set) var consumerApp = "none"
    private(set) var deliveredCards: [UUID:CardKind] = [:]
    private var delivery = FrameDeliveryTracker()
    private static let preference = "selectedWebcamID"

    init() {
        selectedID = UserDefaults.standard.string(forKey: Self.preference) ?? ""
        do { composer = try FrameComposer() }
        catch { cameraStatus = "Couldn’t prepare video. Quit and reopen Callayers." }
        refresh()
    }
    func setSessionAllowed(_ allowed: Bool) {
        sessionAllowed = allowed
        if !allowed {
            output.stop(); delivery.clear(); deliveredCards = [:]; outputActive = false; cameraReady = false
            capture.configure(id: nil, retry: true); requestedID = nil; preview = nil; retainedSample = nil
        }
        refresh(force: true)
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
        if sessionAllowed && !selectedID.isEmpty, AVCaptureDevice.authorizationStatus(for: .video) == .notDetermined {
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
        output.refresh(); outputStatus = output.status; consumerCount = output.consumerCount
        let visible = NSApp?.windows.contains { $0.isVisible && !$0.isMiniaturized && $0.occlusionState.contains(.visible) } ?? true
        previewVisible = visible
        consumerApp = output.consumerApp
        wantsFrames = visible || output.consumerCount > 0
        let exists = cameras.contains { $0.id == selectedID }
        let id = sessionAllowed && wantsFrames && exists && !selectedID.isEmpty ? selectedID : nil
        if id != requestedID || force {
            requestedID = id
            capture.configure(id: id, retry: force)
        }
        if !sessionAllowed { cameraStatus = "Sign in to choose your webcam." }
        else if selectedID.isEmpty { cameraStatus = "Choose your webcam." }
        else if !exists { cameraStatus = "Saved webcam unavailable. Reconnect it or choose another." }
        else if !wantsFrames { cameraStatus = "Camera paused while the preview and output are idle." }
        else if AVCaptureDevice.authorizationStatus(for: .video) != .authorized {
            cameraStatus = "Allow webcam access, then choose Retry camera."
        } else {
            let (image, error) = capture.latest()
            cameraStatus = error.isEmpty ? (image == nil ? "Opening webcam…" : "Webcam live") : error
        }
        cameraReady = sessionAllowed && exists && capture.latest().0 != nil
    }
    func tick(layers: [VideoLayer], previewLayers: [VideoLayer]? = nil, cards: [UUID:CardKind] = [:]) -> Bool {
        guard !terminated else { return false }
        if Date() >= refreshTime { refresh() }
        guard sessionAllowed, wantsFrames, let composer else { outputActive = false; deliveredCards = [:]; return false }
        let camera = capture.latest().0
        do {
            guard let (sample, image) = try composer.compose(camera: camera, layers: layers, at: CMClockGetTime(CMClockGetHostTimeClock())) else { return false }
            retainedSample = sample
            let previewImage = try previewLayers.flatMap { try composer.compose(camera:camera,layers:$0,at:CMClockGetTime(CMClockGetHostTimeClock()))?.1 } ?? image
            preview = NSImage(cgImage: previewImage, size: NSSize(width: 1280, height: 720))
            outputActive = false; deliveredCards = [:]
            if camera != nil && output.consumerCount > 0 {
                if output.sendSample(sample) {
                    let timestamp = CMTimeConvertScale(CMSampleBufferGetPresentationTimeStamp(sample),timescale:1_000_000_000,method:.default).value
                    delivery.record(timestamp:timestamp,cards:cards)
                }
                let delivered = output.deliveredTimestamp
                let now = CMTimeConvertScale(CMClockGetTime(CMClockGetHostTimeClock()),timescale:1_000_000_000,method:.default).value
                let confirmed = delivery.delivered(timestamp:delivered,now:now)
                outputActive = confirmed != nil; deliveredCards = confirmed ?? [:]
                if outputStatus != output.status { outputStatus = output.status }
            }
            return true
        } catch { cameraStatus = "Couldn’t update video. Choose Retry camera."; return false }
    }
    func shutdown() {
        terminated = true; output.stop(); capture.configure(id: nil, retry: true)
        retainedSample = nil; preview = nil
    }
}
