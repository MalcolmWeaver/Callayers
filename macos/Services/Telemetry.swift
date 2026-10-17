import Foundation

struct TelemetryEvent {
    enum Creation: String { case aiCreate = "ai_create", aiRevision = "ai_revision", imageImport = "image_import", annotation }
    let id = UUID().uuidString.lowercased()
    let session: String
    let time: Date
    let version: String
    let name: String
    let properties: [String: Any]
    var object: [String: Any] {
        let formatter = ISO8601DateFormatter(); formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return ["id": id, "sessionId": session, "timestamp": formatter.string(from: time), "appVersion": version,
                "platform": "macos", "event": name, "properties": properties]
    }
}
struct TelemetryVideo: Equatable {
    var preview = false
    var connected = false
    var output = false
    var app = "none"
    var connection = ""
    var previewCards: [UUID: String] = [:]
    var outputCards: [UUID: String] = [:]
    var properties: [String: Any] { ["preview_visible": preview, "consumer_connected": connected,
                                    "output_active": output, "consumer_app": connected ? app : "none"] }
}
struct TelemetryTransitions {
    private(set) var previous: TelemetryVideo?
    mutating func update(_ value: TelemetryVideo) -> [(String, [String: Any])] {
        var events: [(String, [String: Any])] = []
        if let old = previous, old.connected, value.connected, old.connection != value.connection {
            var closed = old; closed.connected = false; closed.output = false; closed.app = "none"; closed.outputCards = [:]
            events += update(closed)
        }
        let old = previous
        if old == nil || old!.preview != value.preview || old!.connected != value.connected || old!.output != value.output || old!.app != value.app {
            events.append(("video_state_changed", value.properties))
        }
        for (destination, before, after) in [("preview", old?.previewCards ?? [:], value.previewCards), ("output", old?.outputCards ?? [:], value.outputCards)] {
            for id in before.keys.sorted(by: { $0.uuidString < $1.uuidString }) where after[id] != before[id] {
                events.append(("card_usage_changed", ["card_id": id.uuidString.lowercased(), "card_type": before[id]!, "destination": destination, "applied": false]))
            }
            for id in after.keys.sorted(by: { $0.uuidString < $1.uuidString }) where before[id] != after[id] {
                events.append(("card_usage_changed", ["card_id": id.uuidString.lowercased(), "card_type": after[id]!, "destination": destination, "applied": true]))
            }
        }
        previous = value
        return events
    }
    mutating func end() -> [(String, [String: Any])] {
        guard var state = previous else { return [] }
        state.preview = false; state.output = false; state.previewCards = [:]; state.outputCards = [:]
        return update(state)
    }
}

@MainActor
final class TelemetrySession {
    private struct Pending { let event: TelemetryEvent; var attempts = 0 }
    private let sessionID = UUID().uuidString.lowercased()
    private let version: String
    private let http: HTTPTransport
    private let clock: () -> Date
    private let sleep: (UInt64) async throws -> Void
    private var grant: AuthGrant
    private var pending: [Pending] = []
    private var flight: [Pending] = []
    private var worker: Task<Void, Never>?
    private var epoch = UUID()
    private var ended = false
    private var waitingForBatch = false
    private var transitions = TelemetryTransitions()
    private(set) var sentCount = 0
    var bufferedCount: Int { pending.count + flight.count }
    init(grant: AuthGrant, http: HTTPTransport = ServiceClient(timeout: 8), version: String? = nil,
         clock: @escaping () -> Date = Date.init,
         sleep: @escaping (UInt64) async throws -> Void = { try await Task.sleep(nanoseconds: $0) }) {
        self.grant = grant; self.http = http; self.clock = clock; self.sleep = sleep
        self.version = version ?? (Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "development")
        append("session_started", [:])
    }
    func renew(_ value: AuthGrant) {
        guard value.saved.uid == grant.saved.uid, value.saved.service == grant.saved.service, value.saved.project == grant.saved.project else { return }
        grant = value
    }
    func video(_ state: TelemetryVideo) {
        guard !ended else { return }
        for (name, properties) in transitions.update(state) { append(name, properties) }
    }
    func created(_ kind: TelemetryEvent.Creation, card: UUID? = nil) {
        guard !ended, kind == .annotation || card != nil else { return }
        var properties: [String: Any] = ["kind": kind.rawValue]
        if kind != .annotation { properties["card_id"] = card!.uuidString.lowercased() }
        append("content_created", properties)
    }
    private func append(_ name: String, _ properties: [String: Any]) {
        while pending.count + flight.count >= 128 && !pending.isEmpty { pending.removeFirst() }
        guard pending.count + flight.count < 128 else { return }
        pending.append(Pending(event: TelemetryEvent(session: sessionID, time: clock(), version: version, name: name, properties: properties)))
        if pending.count >= 32 && waitingForBatch {
            epoch = UUID(); worker?.cancel(); worker = nil; waitingForBatch = false
        }
        start(immediate: pending.count >= 32)
    }
    func finish() {
        guard !ended else { return }
        for (name, properties) in transitions.end() { append(name, properties) }
        ended = true; epoch = UUID(); worker?.cancel(); worker = nil
        pending.insert(contentsOf: flight, at: 0); flight = []
        start(immediate: true)
    }
    private func start(immediate: Bool) {
        guard worker == nil, !pending.isEmpty else { return }
        let ticket = epoch; waitingForBatch = !immediate
        worker = Task { [self] in
            do { if !immediate { try await sleep(2_000_000_000) } } catch { return }
            guard ticket == epoch else { return }
            waitingForBatch = false
            while !pending.isEmpty {
                pending.removeAll { clock().timeIntervalSince($0.event.time) > 300 || $0.attempts >= 3 }
                guard !pending.isEmpty, grant.expires > clock() else { pending = []; break }
                flight = Array(pending.prefix(32)); pending.removeFirst(flight.count)
                for i in flight.indices { flight[i].attempts += 1 }
                let records = flight
                let payload = try? JSONSerialization.data(withJSONObject: ["events": records.map { $0.event.object }], options: [.sortedKeys])
                var retry = false
                if let payload, payload.count <= 32_768 {
                    do {
                        let result = try await http.send(ServiceClient.request(grant.saved.service.appendingPathComponent("events"), method: "POST", body: payload, token: grant.token, timeout: 8), limit: 4096)
                        guard ticket == epoch else { return }
                        if (200...299).contains(result.status) { sentCount += records.count }
                        else { retry = result.status == 429 || result.status >= 500 }
                    } catch {
                        guard ticket == epoch else { return }; retry = true
                    }
                }
                flight = []
                if ended { pending = []; break }
                if retry && records[0].attempts < 3 {
                    pending.insert(contentsOf: records, at: 0)
                    do { try await sleep(records[0].attempts == 1 ? 2_000_000_000 : 10_000_000_000) } catch { return }
                    guard ticket == epoch else { return }
                }
            }
            worker = nil
        }
    }
}
