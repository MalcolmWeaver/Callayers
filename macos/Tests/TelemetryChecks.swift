import Foundation
@main struct TelemetryChecks {
    static func main() throws {
        func check(_ b: Bool,_ m: String) throws { if !b { throw WorkspaceError.invalid(m) } }
        let now = Date()
        var queue = TelemetryQueue()
        for _ in 0..<140 { queue.append(TelemetryEvent(name:"session_started",properties:[:],session:UUID(),date:now)) }
        try check(queue.events.count == 128,"Queue grew beyond bound")
        let batch = queue.batch(now:now)
        try check(batch.count == 32,"Incorrect batch bound")
        let data = try JSONSerialization.data(withJSONObject:["events":batch.map(\.json)])
        let json = String(decoding:data,as:UTF8.self)
        try check(json.contains("macos") && !json.contains("email") && !json.contains("prompt"),"Privacy envelope incorrect")
        queue.expire(now:now.addingTimeInterval(301))
        try check(queue.events.isEmpty,"Old telemetry not dropped")
        let one = TelemetryEvent(name:"session_started",properties:[:],session:UUID(),date:now)
        try check(one.json["id"] as? String == one.id.uuidString.lowercased(),"Stable UUID incorrect")
        print("PASS telemetry queue bounds, batch bounds, expiry, stable event IDs and approved envelope")
    }
}
