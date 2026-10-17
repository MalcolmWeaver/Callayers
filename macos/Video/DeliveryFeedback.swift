import Foundation

// Internal process-to-extension metadata. Only normalized labels enter analytics.
struct DeliveryFeedback: Codable {
    let sourceTime: Int64
    let deliveryTime: Int64
    let connection: String
    let consumerApp: String
    let count: Int
    static let labels: Set<String> = ["none", "unknown", "brave", "chrome", "edge", "firefox", "zoom", "teams", "discord", "skype", "obs", "opera", "windows_camera"]
    static func parse(_ text: String?) -> DeliveryFeedback? {
        guard let text, text.utf8.count <= 1024, let data = text.data(using: .utf8),
              let value = try? JSONDecoder().decode(Self.self, from: data), value.count >= 0, value.count < 1000,
              value.sourceTime >= 0, value.deliveryTime >= 0, UUID(uuidString: value.connection) != nil,
              labels.contains(value.consumerApp), (value.count == 0) == (value.consumerApp == "none") else { return nil }
        return value
    }
    static func label(_ identity: String) -> String {
        let names = ["com.google.Chrome": "chrome", "com.brave.Browser": "brave", "com.microsoft.edgemac": "edge",
                     "org.mozilla.firefox": "firefox", "us.zoom.xos": "zoom", "com.microsoft.teams": "teams", "com.microsoft.teams2": "teams",
                     "com.hnc.Discord": "discord", "com.skype.skype": "skype", "com.obsproject.obs-studio": "obs", "com.operasoftware.Opera": "opera"]
        return names[identity] ?? "unknown"
    }
}
struct DeliveryLedger {
    private struct Entry { let time: Int64; let cards: [UUID: String] }
    private var sent: [Entry] = []
    mutating func record(time: Int64, cards: [UUID: String]) {
        sent.append(Entry(time: time, cards: cards))
        if sent.count > 120 { sent.removeFirst(sent.count - 120) }
    }
    mutating func clear() { sent = [] }
    func delivered(_ feedback: DeliveryFeedback?, now: Int64) -> [UUID: String]? {
        guard let feedback, feedback.count > 0, feedback.sourceTime > 0,
              now >= feedback.deliveryTime, now - feedback.deliveryTime < 1_000_000_000,
              now >= feedback.sourceTime, now - feedback.sourceTime < 1_000_000_000 else { return nil }
        return sent.last(where: { $0.time == feedback.sourceTime })?.cards
    }
}
