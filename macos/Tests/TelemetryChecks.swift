import Foundation

private func require(_ condition: @autoclosure () -> Bool, _ message: String) throws {
    if !condition() { throw ServiceFailure(message: message) }
}
private final class EventHTTP: HTTPTransport {
    var requests: [URLRequest] = []
    var statuses: [Int] = []
    func send(_ request: URLRequest, limit: Int) async throws -> HTTPResult {
        requests.append(request)
        return HTTPResult(status: statuses.isEmpty ? 204 : statuses.removeFirst(), body: Data())
    }
}
@main
struct TelemetryChecks {
    @MainActor static func wait(_ condition: () -> Bool) async throws {
        for _ in 0..<200 {
            if condition() { return }; try await Task.sleep(nanoseconds: 10_000_000)
        }
        throw ServiceFailure(message: "Telemetry fixture timed out")
    }
    @MainActor static func main() async throws {
        let id = UUID()
        var transitions = TelemetryTransitions()
        let state = TelemetryVideo(preview: true, connected: true, output: true, app: "chrome", connection: "one", previewCards: [id: "ai"], outputCards: [id: "ai"])
        let first = transitions.update(state)
        try require(first.count == 3, "Initial usage did not include both destinations")
        try require(transitions.update(state).isEmpty, "Repeated frames emitted events")
        var reconnected = state; reconnected.connection = "two"
        let changed = transitions.update(reconnected)
        try require(changed.filter { $0.0 == "video_state_changed" }.count == 2 && changed.filter { $0.0 == "card_usage_changed" }.count == 2, "Fast reconnect did not close the previous output connection")
        let ended = transitions.end()
        try require(ended.count == 3 && transitions.previous?.connected == true && transitions.previous?.output == false, "Session end invented a consumer disconnect or retained usage")
        print("PASS preview/output transitions, unchanged-frame suppression, fast reconnect and session end")

        let time: Int64 = 100_000_000_000
        var ledger = DeliveryLedger()
        ledger.record(time: time, cards: [id: "image"])
        let feedback = DeliveryFeedback(sourceTime: time, deliveryTime: time + 10, connection: UUID().uuidString, consumerApp: "chrome", count: 1)
        try require(ledger.delivered(feedback, now: time + 100)?[id] == "image", "Fresh delivered metadata was lost")
        try require(ledger.delivered(feedback, now: time + 1_000_000_000) == nil, "Stale frames retained output usage")
        let other = DeliveryFeedback(sourceTime: time - 1, deliveryTime: time + 10, connection: feedback.connection, consumerApp: "chrome", count: 1)
        try require(ledger.delivered(other, now: time + 100) == nil, "Another producer's frame counted as output")
        ledger.clear(); try require(ledger.delivered(feedback, now: time + 100) == nil, "Prior producer session remained active")
        try require(DeliveryFeedback.parse(nil) == nil && DeliveryFeedback.parse("{}") == nil, "Missing/legacy feedback was trusted")
        let parsed = DeliveryFeedback.parse(String(data: try JSONEncoder().encode(feedback), encoding: .utf8))
        try require(parsed?.sourceTime == time && DeliveryFeedback.label("com.google.Chrome") == "chrome" && DeliveryFeedback.label("/private/user/path") == "unknown", "Feedback schema or consumer normalization failed")
        print("PASS delivered-frame ownership, expiry, producer reset and normalized consumer identity")

        let grant = AuthGrant(saved: SavedSession(service: ServiceClient.production, project: "fixture", uid: "fixture-user", refresh: "refresh-secret"), token: "access-secret", email: "private@example.invalid", expires: Date().addingTimeInterval(3600))
        let http = EventHTTP(); http.statuses = [503, 204]
        var delays: [UInt64] = []
        let sender = TelemetrySession(grant: grant, http: http, version: "0.4.0", sleep: { delays.append($0); await Task.yield() })
        sender.video(state); sender.created(.imageImport, card: id)
        try await wait { sender.sentCount == 5 }
        try require(http.requests.count == 2 && http.requests[0].httpBody == http.requests[1].httpBody, "Retries changed event IDs or timestamps")
        let body = http.requests[0].httpBody!
        let json = try JSONSerialization.jsonObject(with: body) as! [String: Any]
        let events = json["events"] as! [[String: Any]]
        try require(events.count == 5 && Set(events[0].keys) == Set(["id", "sessionId", "timestamp", "appVersion", "platform", "event", "properties"]), "Telemetry envelope changed")
        let text = String(data: body, encoding: .utf8)!
        for secret in ["private@example.invalid", "refresh-secret", "access-secret", "fixture-user"] { try require(!text.contains(secret), "Private identity or credential entered event payload") }
        try require(http.requests[0].value(forHTTPHeaderField: "Authorization") == "Bearer access-secret", "Events were unauthenticated")
        if CommandLine.arguments.count > 1 { try body.write(to: URL(fileURLWithPath: CommandLine.arguments[1])) }
        let outage = EventHTTP(); outage.statuses = [500, 500, 500, 204]
        let failed = TelemetrySession(grant: grant, http: outage, sleep: { _ in await Task.yield() })
        try await wait { outage.requests.count == 3 && failed.bufferedCount == 0 }
        try require(outage.requests.count == 3, "Retry limit exceeded")
        let pressure = EventHTTP()
        let bounded = TelemetrySession(grant: grant, http: pressure, sleep: { _ in try await Task.sleep(nanoseconds: 20_000_000) })
        for _ in 0..<500 { bounded.created(.annotation) }
        try require(bounded.bufferedCount <= 128, "Offline queue was unbounded")
        bounded.finish(); try await wait { bounded.bufferedCount == 0 }
        print("PASS bounded batching, stable retries, attempt limits, authenticated delivery and private-data exclusion")
    }
}
