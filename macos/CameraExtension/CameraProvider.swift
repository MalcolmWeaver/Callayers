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
        let queue = DispatchQueue(label: "com.callayers.camera", qos: .userInitiated, autoreleaseFrequency: .workItem)
        provider = CMIOExtensionProvider(source: self, clientQueue: queue)
        do {
            camera = try CameraDevice(queue: queue)
            try provider.addDevice(camera.device)
        } catch { fatalError("Cannot create Callayers camera: \(error)") }
    }

    func connect(to client: CMIOExtensionClient) throws {}
    func disconnect(from client: CMIOExtensionClient) { camera.disconnect(client) }
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
    private var input: CameraStream!
    private let picture: TestPicture
    private let queue: DispatchQueue
    private var timer: DispatchSourceTimer?
    private var streamCount = 0
    private var producer: CMIOExtensionClient?
    private var receiving = false
    private var consuming = false
    private var generation: UInt = 0
    private let latest = LatestVideoFrame()
    static let consumerProperty = CMIOExtensionProperty(rawValue: "4cc_cact_glob_0000")
    private let logger = Logger(subsystem: "com.callayers.renderer-preview.camera", category: "Video")

    init(queue: DispatchQueue) throws {
        self.queue = queue
        picture = try TestPicture(offline: true)
        super.init()
        device = CMIOExtensionDevice(localizedName: "Callayers",
            deviceID: UUID(uuidString: "38414F57-EB9F-4D53-A6F2-071F95FE7E72")!,
            legacyDeviceID: "com.callayers.virtual-camera", source: self)
        let format = CMIOExtensionStreamFormat(formatDescription: picture.format,
            maxFrameDuration: TestPicture.frameDuration, minFrameDuration: TestPicture.frameDuration, validFrameDurations: nil)
        video = CameraStream(device: self, format: format, sink: false)
        try device.addStream(video.stream)
        input = CameraStream(device: self, format: format, sink: true)
        try device.addStream(input.stream)
    }

    var availableProperties: Set<CMIOExtensionProperty> { [.deviceTransportType, .deviceModel, Self.consumerProperty] }
    func deviceProperties(forProperties properties: Set<CMIOExtensionProperty>) throws -> CMIOExtensionDeviceProperties {
        let result = CMIOExtensionDeviceProperties(dictionary: [:])
        if properties.contains(.deviceTransportType) { result.transportType = kIOAudioDeviceTransportTypeVirtual }
        if properties.contains(.deviceModel) { result.model = "Callayers Virtual Camera" }
        if properties.contains(Self.consumerProperty) {
            result.setPropertyState(CMIOExtensionPropertyState(value: String(streamCount) as NSString), forProperty: Self.consumerProperty)
        }
        return result
    }
    func setDeviceProperties(_ properties: CMIOExtensionDeviceProperties) throws {}

    func start() {
        streamCount += 1
        notifyConsumers()
        ensureTimer()
        logger.notice("Camera stream started")
    }
    private func ensureTimer() {
        guard timer == nil else { return }
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(deadline: .now(), repeating: .nanoseconds(1_000_000_000 / 30), leeway: .milliseconds(2))
        timer.setEventHandler { [weak self] in self?.sendFrame() }
        self.timer = timer
        timer.resume()
    }

    func stop() {
        streamCount = max(0, streamCount - 1)
        notifyConsumers()
        stopTimerIfIdle()
        logger.notice("Camera stream stopped")
    }
    private func stopTimerIfIdle() {
        guard streamCount == 0 && !receiving else { return }
        timer?.cancel()
        timer = nil
        latest.clear()
    }
    private func notifyConsumers() {
        device.notifyPropertiesChanged([Self.consumerProperty: CMIOExtensionPropertyState<AnyObject>(value: String(streamCount) as NSString)])
    }
    func authorizeProducer(_ client: CMIOExtensionClient) -> Bool {
        if #available(macOS 13, *), client.signingID != "com.callayers.renderer-preview" { return false }
        guard producer == nil || producer?.clientID == client.clientID else { return false }
        producer = client
        return true
    }
    func startReceiving() {
        guard producer != nil else { return }
        receiving = true
        ensureTimer()
    }
    func stopReceiving() {
        receiving = false; producer = nil; consuming = false; generation &+= 1
        latest.clear(); stopTimerIfIdle()
    }
    func disconnect(_ client: CMIOExtensionClient) {
        if producer?.clientID == client.clientID { stopReceiving() }
    }
    private func consume() {
        guard receiving, !consuming, let client = producer else { return }
        consuming = true
        let epoch = generation
        input.stream.consumeSampleBuffer(from: client) { [weak self] sample, sequence, _, _, _ in
            guard let self else { return }
            self.queue.async { [weak self] in
                guard let self, self.generation == epoch else { return }
                self.consuming = false
                guard self.receiving, let sample else { return }
                let now = CMClockGetTime(CMClockGetHostTimeClock())
                self.latest.accept(sample, now: now)
                let nanos = CMTimeConvertScale(now, timescale: 1_000_000_000, method: .default).value
                self.input.stream.notifyScheduledOutputChanged(CMIOExtensionScheduledOutput(sequenceNumber: sequence,
                    hostTimeInNanoseconds: UInt64(max(0, nanos))))
            }
        }
    }

    private func sendFrame() {
        consume()
        guard streamCount > 0 else { return }
        let now = CMClockGetTime(CMClockGetHostTimeClock())
        do {
            let sample: CMSampleBuffer
            if let incoming = latest.sample(now: now), let image = CMSampleBufferGetImageBuffer(incoming) {
                var timing = CMSampleTimingInfo(duration: TestPicture.frameDuration, presentationTimeStamp: now, decodeTimeStamp: .invalid)
                var retimed: CMSampleBuffer?
                let status = CMSampleBufferCreateReadyWithImageBuffer(allocator: nil, imageBuffer: image,
                    formatDescription: picture.format, sampleTiming: &timing, sampleBufferOut: &retimed)
                guard status == noErr, let retimed else { return }
                sample = retimed
            } else {
                guard let fallback = try picture.sample(at: now) else { return }
                sample = fallback
            }
            let nanos = CMTimeConvertScale(now, timescale: 1_000_000_000, method: .default).value
            video.stream.send(sample, discontinuity: [], hostTimeInNanoseconds: UInt64(max(0, nanos)))
        } catch { logger.error("Frame generation failed: \(String(describing: error), privacy: .public)") }
    }
}

final class CameraStream: NSObject, CMIOExtensionStreamSource {
    private(set) var stream: CMIOExtensionStream!
    private unowned let device: CameraDevice
    private let sink: Bool
    let formats: [CMIOExtensionStreamFormat]
    init(device: CameraDevice, format: CMIOExtensionStreamFormat, sink: Bool) {
        self.device = device
        self.sink = sink
        formats = [format]
        super.init()
        stream = CMIOExtensionStream(localizedName: sink ? "Callayers Workspace Input" : "Callayers Video",
            streamID: UUID(uuidString: sink ? "7ED7D46D-69EA-4E02-8416-E4D2E29A367B" : "F056C4D7-86F8-4E46-8E6C-7885C0C377D7")!,
            direction: sink ? .sink : .source, clockType: .hostTime, source: self)
    }
    var availableProperties: Set<CMIOExtensionProperty> {
        var properties: Set<CMIOExtensionProperty> = [.streamActiveFormatIndex, .streamFrameDuration]
        if sink { properties.formUnion([.streamSinkBufferQueueSize, .streamSinkBuffersRequiredForStartup, .streamSinkEndOfData]) }
        return properties
    }
    func streamProperties(forProperties properties: Set<CMIOExtensionProperty>) throws -> CMIOExtensionStreamProperties {
        let result = CMIOExtensionStreamProperties(dictionary: [:])
        if properties.contains(.streamActiveFormatIndex) { result.activeFormatIndex = 0 }
        if properties.contains(.streamFrameDuration) { result.frameDuration = TestPicture.frameDuration }
        if properties.contains(.streamSinkBufferQueueSize) { result.sinkBufferQueueSize = 2 }
        if properties.contains(.streamSinkBuffersRequiredForStartup) { result.sinkBuffersRequiredForStartup = 1 }
        if properties.contains(.streamSinkEndOfData) { result.sinkEndOfData = 0 }
        return result
    }
    func setStreamProperties(_ properties: CMIOExtensionStreamProperties) throws {
        if let index = properties.activeFormatIndex, index != 0 { throw CameraError.unsupportedFormat }
        if let duration = properties.frameDuration, CMTimeCompare(duration, TestPicture.frameDuration) != 0 {
            throw CameraError.unsupportedFormat
        }
        if let end = properties.sinkEndOfData, end != 0 { device.stopReceiving() }
    }
    func authorizedToStartStream(for client: CMIOExtensionClient) -> Bool { !sink || device.authorizeProducer(client) }
    func startStream() throws { if sink { device.startReceiving() } else { device.start() } }
    func stopStream() throws { if sink { device.stopReceiving() } else { device.stop() } }
}
