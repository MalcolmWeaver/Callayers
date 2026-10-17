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
    weak var previewWindow: NSWindow?
    var onTelemetry: ((TelemetryVideo) -> Void)?
    private var previewVisible = false
    private var previewCards: [UUID: String] = [:]
    private var previewTime = Date.distantPast
    private var feedbackTime = Date.distantPast
    private var feedback: DeliveryFeedback?
    private var ledger = DeliveryLedger()
    private let capture = CameraCapture()
    private let output = CLVirtualCameraClient()
    private var composer: FrameComposer?
    private var retainedSample: CMSampleBuffer?
    private var requestedID: String?
    private var refreshTime = Date.distantPast
    private var terminated = false
    private var authorized = true
    var hasLiveCamera: Bool { capture.latest().0 != nil }
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
        guard !terminated, authorized else { return }
        refreshTime = Date().addingTimeInterval(1)
        cameras = CameraCapture.devices().map { WebcamChoice(id: $0.uniqueID, name: $0.localizedName) }
        output.refresh(); outputStatus = output.status
        let visible = previewWindow.map { $0.isVisible && !$0.isMiniaturized && $0.occlusionState.contains(.visible) } ?? (NSApp?.windows.contains { $0.isVisible && !$0.isMiniaturized && $0.occlusionState.contains(.visible) } ?? true)
        previewVisible = visible
        wantsFrames = visible || output.consumerCount > 0
        reportTelemetry()
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
    var hasConsumers: Bool { output.consumerCount > 0 }
    func refreshDemand() { if Date() >= refreshTime { refresh() } }
    private func reportTelemetry() {
        let now = CMTimeConvertScale(CMClockGetTime(CMClockGetHostTimeClock()), timescale: 1_000_000_000, method: .default).value
        if Date() >= feedbackTime {
            feedbackTime = Date().addingTimeInterval(0.25)
            feedback = DeliveryFeedback.parse(output.deliveryState)
        }
        let freshFeedback = feedback.flatMap { now >= $0.deliveryTime && now - $0.deliveryTime < 1_000_000_000 ? $0 : nil }
        let connected = freshFeedback.map { $0.count > 0 } ?? (output.consumerCount > 0)
        let delivered = authorized ? ledger.delivered(freshFeedback, now: now) : nil
        let previewFresh = authorized && previewVisible && Date().timeIntervalSince(previewTime) < 1
        let state = TelemetryVideo(preview: authorized && previewVisible, connected: connected,
            output: connected && delivered != nil, app: connected ? (freshFeedback?.consumerApp ?? "unknown") : "none",
            connection: freshFeedback?.connection ?? "legacy", previewCards: previewFresh ? previewCards : [:], outputCards: delivered ?? [:])
        onTelemetry?(state)
    }
    func tick(layers: [VideoLayer], previewLayers: [VideoLayer]? = nil) -> Bool {
        guard !terminated, authorized else { return false }
        if Date() >= refreshTime { refresh() }
        defer { reportTelemetry() }
        guard wantsFrames, let composer else { return false }
        guard let camera = capture.latest().0 else {
            let hadPreview = preview != nil; preview = nil; retainedSample = nil; return hadPreview
        }
        do {
            guard let (sample, image) = try composer.compose(camera: camera, layers: layers, at: CMClockGetTime(CMClockGetHostTimeClock())) else { return false }
            retainedSample = sample
            let cards = Dictionary(uniqueKeysWithValues: layers.compactMap { layer in layer.cardID.map { ($0, layer.cardType) } })
            if let previewLayers {
                if let previewImage = try composer.compose(camera: camera, layers: previewLayers, at: CMClockGetTime(CMClockGetHostTimeClock()))?.1 {
                    previewCards = cards; previewTime = Date()
                    preview = NSImage(cgImage: previewImage, size: NSSize(width: 1280, height: 720))
                }
            } else { previewCards = cards; previewTime = Date(); preview = NSImage(cgImage: image, size: NSSize(width: 1280, height: 720)) }
            if output.consumerCount > 0 {
                if output.sendSample(sample) {
                    let time = CMTimeConvertScale(CMSampleBufferGetPresentationTimeStamp(sample), timescale: 1_000_000_000, method: .default).value
                    ledger.record(time: time, cards: cards)
                }
                if outputStatus != output.status { outputStatus = output.status }
            }
            return true
        } catch { cameraStatus = error.localizedDescription; return false }
    }
    func setAuthorized(_ allowed: Bool) {
        authorized = allowed
        if allowed { refresh(force: true) }
        else {
            wantsFrames = false; output.stop(); capture.configure(id: nil, retry: true)
            requestedID = nil; retainedSample = nil; preview = nil
            ledger.clear(); previewCards = [:]; previewTime = .distantPast; reportTelemetry()
        }
    }
    func shutdown() {
        authorized = false; ledger.clear(); previewTime = .distantPast; reportTelemetry()
        terminated = true; output.stop(); capture.configure(id: nil, retry: true)
        retainedSample = nil; preview = nil
    }
}
