import Foundation
@main struct DeliveryChecks {
    static func main() throws {
        func check(_ b: Bool,_ m: String) throws { if !b { throw WorkspaceError.invalid(m) } }
        var tracker = FrameDeliveryTracker()
        let card = UUID(),now: Int64 = 10_000_000_000
        tracker.record(timestamp:now,cards:[card:.image])
        try check(tracker.delivered(timestamp:now-1,now:now) == nil,"Unknown frame reported as delivered")
        try check(tracker.delivered(timestamp:now,now:now+500_000_000)?[card] == .image,"Delivered metadata lost")
        try check(tracker.delivered(timestamp:now,now:now+1_000_000_001) == nil,"Stale delivery stayed active")
        try check(tracker.delivered(timestamp:now,now:now-1) == nil,"Future receipt accepted")
        tracker.clear(); try check(tracker.delivered(timestamp:now,now:now) == nil,"Old session receipt retained")
        for i in 0..<200 { tracker.record(timestamp:now+Int64(i),cards:[:]) }
        try check(tracker.count <= 120,"Frame metadata was unbounded")
        try check(!WorkspaceLifecycle.shouldRender(allowed:false,camera:true,visible:true,consumer:true,locked:false),"Signed-out cards ran")
        try check(WorkspaceLifecycle.shouldRender(allowed:true,camera:true,visible:false,consumer:true,locked:false),"Minimized call stopped")
        try check(!WorkspaceLifecycle.shouldRender(allowed:true,camera:true,visible:false,consumer:false,locked:false),"Idle hidden cards ran")
        print("PASS delivery receipts, expiry, session clearing, bounded metadata and minimized/sign-out render lifecycle")
    }
}
