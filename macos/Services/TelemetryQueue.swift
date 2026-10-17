import Foundation

struct TelemetryEvent {
    let id = UUID()
    let name: String
    let properties: [String:Any]
    let session: UUID
    let date: Date
    var json: [String:Any] {
        let formatter = ISO8601DateFormatter(); formatter.formatOptions = [.withInternetDateTime,.withFractionalSeconds]
        return ["id":id.uuidString.lowercased(),"sessionId":session.uuidString.lowercased(),"timestamp":formatter.string(from:date),
            "appVersion":Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "0.4.0",
            "platform":"macos","event":name,"properties":properties]
    }
}
struct TelemetryQueue {
    private(set) var events: [TelemetryEvent] = []
    mutating func append(_ event: TelemetryEvent) { events.append(event); if events.count > 128 { events.removeFirst(events.count-128) } }
    mutating func expire(now: Date) { events.removeAll { now.timeIntervalSince($0.date) > 300 } }
    mutating func remove(_ ids: Set<UUID>) { events.removeAll { ids.contains($0.id) } }
    func batch(now: Date) -> [TelemetryEvent] { Array(events.filter { now.timeIntervalSince($0.date) <= 300 }.prefix(32)) }
}
struct VideoTelemetryState: Equatable {
    let preview: Bool; let connected: Bool; let active: Bool; let app: String
    var properties: [String:Any] { ["preview_visible":preview,"consumer_connected":connected,"output_active":active,"consumer_app":app] }
}
