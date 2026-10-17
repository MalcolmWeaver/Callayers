import Foundation
@main struct DomainChecks {
    static func main() throws {
        func check(_ value: Bool, _ message: String) throws { if !value { throw WorkspaceError.invalid(message) } }
        var card = WorkspaceCard(name: "Agenda", html: "<h1>Agenda</h1>")
        card.contentWidth = 300; card.contentHeight = 180
        card.place(x: 1200, y: 700, scale: 2)
        try check(card.x + card.width <= 1280 && card.y + card.height <= 720, "Card outside scene")
        let original = card.content
        let revision = card.revision
        let replacement = CardContent(name: "New", html: "<p>New</p>", width: 400, height: 250)
        try check(CardRevision.apply(to: &card, expectedRevision: revision, content: replacement), "Revision rejected")
        try check(card.previous == original && card.state == "{}", "Missing backup or state reset")
        try check(!CardRevision.apply(to: &card, expectedRevision: revision, content: original), "Late response overwrote content")
        let placement = [card.x, card.y, card.scale]
        try check(CardRevision.restore(&card), "Restore unavailable")
        try check(card.content == original && card.previous == nil && placement == [card.x, card.y, card.scale], "Restore changed placement or did not consume backup")
        let legacy = Data("{\"version\":1,\"cards\":[]}".utf8)
        let old = try JSONDecoder().decode(WorkspaceDocument.self, from: legacy).validated()
        try check(old.annotations.isEmpty && old.prompt.isEmpty, "Legacy document failed")
        var doc = WorkspaceDocument(); doc.cards = [card]; doc.prompt = "Create an agenda"
        let data = try JSONEncoder().encode(doc)
        try check(try JSONDecoder().decode(WorkspaceDocument.self, from: data).validated() == doc, "New document did not round trip")
        card.contentWidth = 0; doc.cards = [card]
        do { _ = try doc.validated(); throw NSError(domain: "Accepted invalid dimension", code: 1) }
        catch WorkspaceError.invalid { }
        print("PASS dimensions, stale revisions, backup restore, legacy decoding and workspace round trip")
    }
}
