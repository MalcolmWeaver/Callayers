import Foundation

// Metadata is associated with the exact producer timestamp acknowledged by the
// extension, rather than with selection or the frame most recently queued.
struct FrameDeliveryTracker {
    private var frames: [Int64:[UUID:CardKind]] = [:]
    var count: Int { frames.count }
    mutating func record(timestamp: Int64,cards: [UUID:CardKind]) {
        frames[timestamp] = cards
        frames = frames.filter { timestamp >= $0.key && timestamp-$0.key < 2_000_000_000 }
        if frames.count > 120 { for key in frames.keys.sorted().prefix(frames.count-120) { frames[key] = nil } }
    }
    func delivered(timestamp: Int64,now: Int64) -> [UUID:CardKind]? {
        guard timestamp > 0,now >= timestamp,now-timestamp < 1_000_000_000 else { return nil }
        return frames[timestamp]
    }
    mutating func clear() { frames = [:] }
}
enum WorkspaceLifecycle {
    static func shouldRender(allowed: Bool,camera: Bool,visible: Bool,consumer: Bool,locked: Bool) -> Bool {
        allowed && camera && (visible || consumer) && !locked
    }
}
