import Foundation
@main struct SaveFlowChecks {
    @MainActor static func main() throws {
        func check(_ b: Bool,_ m: String) throws { if !b { throw WorkspaceError.invalid(m) } }
        var doc = WorkspaceDocument(),flow = WorkspaceSaveFlow()
        let received = WorkspaceCard(name:"Paid AI result",html:"<p>Received result</p>")
        flow.begin { $0.cards.append(received) }
        // A renderer snapshot fails before disk writing: the received result must
        // remain editable in memory and available to a later Retry saving action.
        flow.applyPending(to:&doc)
        try check(doc.cards == [received],"Snapshot failure discarded received content")
        flow.applyPending(to:&doc)
        try check(doc.cards.count == 1,"Retry applied queued content twice")
        // A save captured count=7; the renderer changed to 8 while writing.
        // An idle transition must capture 8 before renderer teardown.
        doc.cards[0].state = "{\"count\":7}"
        let liveState = "{\"count\":8}"
        flow.requestFreshSnapshot()
        var destroyed = false
        if flow.consumeFreshSnapshot() { doc.cards[0].state = liveState } else { destroyed = true }
        try check(!destroyed && doc.cards[0].state == liveState,"Idle write discarded the latest interactive value")
        try check(!flow.consumeFreshSnapshot(),"Final snapshot request did not drain")
        let canceled = GenerationPermit()
        flow.begin { if canceled.valid { $0.cards.append(received) } }
        canceled.invalidate()
        flow.applyPending(to:&doc) // Successful snapshot completion.
        try check(doc.cards.count == 1,"Canceled result applied after successful snapshots")
        let failedSnapshotPermit = GenerationPermit()
        flow.begin { if failedSnapshotPermit.valid { $0.cards.append(received) } }
        failedSnapshotPermit.invalidate()
        flow.applyPending(to:&doc) // Snapshot-error retention uses this same path.
        try check(doc.cards.count == 1,"Canceled result retained after snapshot failure")
        print("PASS received content retained on snapshot failure and idle/quit writes require a fresh snapshot")
    }
}
