import Foundation
import CoreMediaIO
import IOKit.audio
import os

final class CameraProvider: NSObject, CMIOExtensionProviderSource {
    private(set) var provider: CMIOExtensionProvider!
    private var camera: CameraDevice!

    override init() {
        super.init()
        // Provider callbacks and frame generation share one serial queue.
        let queue = DispatchQueue(label: "com.callayers.camera", qos: .userInitiated)
        provider = CMIOExtensionProvider(source: self, clientQueue: queue)
        do {
            camera = try CameraDevice(queue: queue)
            try provider.addDevice(camera.device)
        } catch { fatalError("Cannot create Callayers camera: \(error)") }
    }

    func connect(to client: CMIOExtensionClient) throws {}
    func disconnect(from client: CMIOExtensionClient) {}
    var availableProperties: Set<CMIOExtensionProperty> { [.providerManufacturer] }
    func providerProperties(forProperties properties: Set<CMIOExtensionProperty>) throws -> CMIOExtensionProviderProperties {
        let result = CMIOExtensionProviderProperties(dictionary: [:])
        if properties.contains(.providerManufacturer) { result.manufacturer = "Callayers" }
        return result
    }
    func setProviderProperties(_ properties: CMIOExtensionProviderProperties) throws {}
}

final class CameraDevice: NSObject, CMIOExtensionDeviceSource {
    private(set) var device: CMIOExtensionDevice!
    private var video: CameraStream!
    private let picture: TestPicture
    private let queue: DispatchQueue
    private var timer: DispatchSourceTimer?
    private var streamCount = 0
    private let logger = Logger(subsystem: "com.callayers.renderer-preview.camera", category: "Video")

    init(queue: DispatchQueue) throws {
        self.queue = queue
        picture = try TestPicture()
        super.init()
        device = CMIOExtensionDevice(localizedName: "Callayers",
            deviceID: UUID(uuidString: "38414F57-EB9F-4D53-A6F2-071F95FE7E72")!,
            legacyDeviceID: "com.callayers.virtual-camera", source: self)
        let format = CMIOExtensionStreamFormat(formatDescription: picture.format,
            maxFrameDuration: TestPicture.frameDuration, minFrameDuration: TestPicture.frameDuration, validFrameDurations: nil)
        video = CameraStream(device: self, format: format)
        try device.addStream(video.stream)
    }

    var availableProperties: Set<CMIOExtensionProperty> { [.deviceTransportType, .deviceModel] }
    func deviceProperties(forProperties properties: Set<CMIOExtensionProperty>) throws -> CMIOExtensionDeviceProperties {
        let result = CMIOExtensionDeviceProperties(dictionary: [:])
        if properties.contains(.deviceTransportType) { result.transportType = kIOAudioDeviceTransportTypeVirtual }
        if properties.contains(.deviceModel) { result.model = "Callayers Virtual Camera" }
        return result
    }
    func setDeviceProperties(_ properties: CMIOExtensionDeviceProperties) throws {}

    func start() {
        streamCount += 1
        guard timer == nil else { return }
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(deadline: .now(), repeating: .nanoseconds(1_000_000_000 / 30), leeway: .milliseconds(2))
        timer.setEventHandler { [weak self] in self?.sendFrame() }
        self.timer = timer
        timer.resume()
        logger.notice("Camera stream started")
    }

    func stop() {
        streamCount = max(0, streamCount - 1)
        guard streamCount == 0 else { return }
        timer?.cancel()
        timer = nil
        logger.notice("Camera stream stopped")
    }

    private func sendFrame() {
        let now = CMClockGetTime(CMClockGetHostTimeClock())
        do {
            guard let sample = try picture.sample(at: now) else { return }
            let nanos = CMTimeConvertScale(now, timescale: 1_000_000_000, method: .default).value
            video.stream.send(sample, discontinuity: [], hostTimeInNanoseconds: UInt64(max(0, nanos)))
        } catch { logger.error("Frame generation failed: \(String(describing: error), privacy: .public)") }
    }
}

final class CameraStream: NSObject, CMIOExtensionStreamSource {
    private(set) var stream: CMIOExtensionStream!
    private unowned let device: CameraDevice
    let formats: [CMIOExtensionStreamFormat]
    init(device: CameraDevice, format: CMIOExtensionStreamFormat) {
        self.device = device
        formats = [format]
        super.init()
        stream = CMIOExtensionStream(localizedName: "Callayers Video",
            streamID: UUID(uuidString: "F056C4D7-86F8-4E46-8E6C-7885C0C377D7")!,
            direction: .source, clockType: .hostTime, source: self)
    }
    var availableProperties: Set<CMIOExtensionProperty> { [.streamActiveFormatIndex, .streamFrameDuration] }
    func streamProperties(forProperties properties: Set<CMIOExtensionProperty>) throws -> CMIOExtensionStreamProperties {
        let result = CMIOExtensionStreamProperties(dictionary: [:])
        if properties.contains(.streamActiveFormatIndex) { result.activeFormatIndex = 0 }
        if properties.contains(.streamFrameDuration) { result.frameDuration = TestPicture.frameDuration }
        return result
    }
    func setStreamProperties(_ properties: CMIOExtensionStreamProperties) throws {
        if let index = properties.activeFormatIndex, index != 0 { throw CameraError.unsupportedFormat }
        if let duration = properties.frameDuration, CMTimeCompare(duration, TestPicture.frameDuration) != 0 {
            throw CameraError.unsupportedFormat
        }
    }
    func authorizedToStartStream(for client: CMIOExtensionClient) -> Bool { true }
    func startStream() throws { device.start() }
    func stopStream() throws { device.stop() }
}
