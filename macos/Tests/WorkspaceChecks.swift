import Foundation

@main
struct WorkspaceChecks {
    static func require(_ condition: @autoclosure () -> Bool, _ message: String) throws {
        if !condition() { throw WorkspaceError.invalid(message) }
    }
    static func rejects(_ action: () throws -> Void, _ message: String) throws {
        var rejected = false
        do { try action() } catch { rejected = true }
        try require(rejected, message)
    }
    static func main() throws {
        let fm = FileManager.default
        let directory = fm.temporaryDirectory.appendingPathComponent("callayers-workspace-\(UUID().uuidString)")
        defer { try? fm.removeItem(at: directory) }
        let store = WorkspaceStore(directory: directory)
        var document = try store.load()
        try require(document.cards.isEmpty, "A new workspace was not empty")
        var first = WorkspaceCard(name: "Counter", html: "<p>One</p>")
        first.state = "{\"count\":7,\"note\":\"saved\"}"
        var second = WorkspaceCard(name: "Counter", html: "<p>Two</p>")
        second.visible = false; second.state = "{\"count\":42}"
        second.place(x: 400, y: 200, scale: 0.75)
        document.cards = [first, second]; document.selectedID = second.id
        try store.save(document)
        let reopened = try WorkspaceStore(directory: directory).load()
        try require(reopened == document, "Card identities, visibility, placement, order, selection or state did not round-trip")
        document.cards.swapAt(0, 1)
        document.cards[1].name = "Renamed"
        try store.save(document)
        let restored = try store.load()
        try require(restored.cards.map(\.id) == [second.id, first.id], "Stacking order was lost")
        try require(restored.cards[1].state == first.state, "Renaming lost interactive state")
        print("PASS multiple cards, independent values, placement, selection and stacking survive reopening")

        var versions = document.cards[0]
        let old = versions.versionSnapshot, oldID = versions.id, oldX = versions.x, oldY = versions.y
        try require(versions.revise(html: "<p>Revised</p>", width: versions.canvasWidth, height: versions.canvasHeight, expectedRevision: nil), "First revision was rejected")
        try require(versions.previousVersion == old && versions.state == "{}", "Revision did not retain exactly one prior version or reset state")
        try require(!versions.revise(html: "late", width: 640, height: 400, expectedRevision: nil), "A late revision overwrote newer content")
        document.cards[0] = versions; try store.save(document)
        var versionReopen = try store.load()
        try require(versionReopen.cards[0].restorePreviousVersion(), "Previous version was lost across reopening")
        let undone = versionReopen.cards[0]
        try require(undone.versionSnapshot == old && undone.id == oldID && undone.x == oldX && undone.y == oldY && undone.previousVersion == nil, "Restoration changed placement/identity or retained redo")
        try require(!versionReopen.cards[0].restorePreviousVersion(), "Restoration could be repeated")
        document = restored
        try store.save(document)
        document.reorder(first.id, .back)
        try require(document.cards.first?.id == first.id, "Send to back did not change drawing order")
        document.reorder(first.id, .front)
        try require(document.cards.last?.id == first.id, "Bring to front did not change drawing order")
        print("PASS version persistence, late-revision rejection, one-shot restoration and stacking changes")

        var invalid = document
        invalid.cards.append(invalid.cards[0])
        try rejects({ try store.save(invalid) }, "Duplicate identities were accepted")
        invalid = document; invalid.cards[0].state = "{\"__proto__\":{}}"
        try rejects({ try store.save(invalid) }, "Prototype-bearing state was accepted")
        invalid = document; invalid.cards[0].x = 1280
        try rejects({ try store.save(invalid) }, "Out-of-scene placement was accepted")
        invalid = document; invalid.version = 99
        try rejects({ try store.save(invalid) }, "A future workspace schema was accepted")
        var card = first; card.place(x: 9999, y: -50, scale: 1.4)
        try require(card.x + card.width <= 1280 && card.y == 0, "Resizing did not keep the card in the scene")
        try require(!WorkspaceDocument.validState("[]"), "A non-object saved state was accepted")
        try require(!WorkspaceDocument.validState("{\"a\":\"" + String(repeating: "x", count: 4097) + "\"}"), "An oversized string was accepted")
        print("PASS invalid saved values, identities, geometry and unsupported versions are rejected")

        let expectedBackup = try JSONDecoder().decode(WorkspaceDocument.self, from: Data(contentsOf: store.backup)).validated()
        let corruption = Data("not a workspace".utf8)
        try corruption.write(to: store.file, options: .atomic)
        let failed = WorkspaceStore(directory: directory)
        try rejects({ _ = try failed.load() }, "A corrupt workspace opened as an empty library")
        try rejects({ try failed.save(WorkspaceDocument()) }, "A failed load permitted overwriting the original")
        let original = try Data(contentsOf: store.file)
        try require(original == corruption, "The unreadable original was modified")
        try require(failed.canRecoverBackup, "A valid backup was unavailable")
        let recovered = try failed.recoverBackup()
        try require(recovered == expectedBackup, "Recovery did not restore the previous saved workspace")
        let preserved = try fm.contentsOfDirectory(at: directory, includingPropertiesForKeys: nil).filter { $0.lastPathComponent.hasPrefix("workspace-preserved-") }
        try require(preserved.count == 1, "Recovery did not preserve the unreadable original")
        let preservedBytes = try Data(contentsOf: preserved[0])
        try require(preservedBytes == corruption, "Recovery changed the preserved original")
        try fm.removeItem(at: failed.file)
        try rejects({ _ = try failed.load() }, "A missing primary silently discarded an existing backup")
        print("PASS failed loads prevent overwrite; backup recovery preserves the original")

        _ = try failed.recoverBackup()
        let before = try Data(contentsOf: failed.file)
        try fm.removeItem(at: failed.backup)
        try fm.createDirectory(at: failed.backup, withIntermediateDirectories: false)
        var changed = recovered; changed.cards[0].name = "Unsaved"
        try rejects({ try failed.save(changed) }, "An obstructed backup did not report a write failure")
        let after = try Data(contentsOf: failed.file)
        try require(before == after, "A write failure damaged the last saved primary")
        print("PASS failed writes leave the last saved workspace intact")
    }
}
