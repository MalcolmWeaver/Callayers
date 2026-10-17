import Foundation

enum CardRevision {
    static func apply(to card: inout WorkspaceCard, expectedRevision: UUID, content: CardContent) -> Bool {
        guard card.revision == expectedRevision, content.valid(), card.image == content.image else { return false }
        card.previous = card.content
        if !card.renamed { card.name = content.name }
        card.html = content.html; card.contentWidth = content.width; card.contentHeight = content.height
        card.state = "{}"; card.revision = UUID(); card.edited = Date(); card.place()
        return true
    }
    static func restore(_ card: inout WorkspaceCard) -> Bool {
        guard let old = card.previous, old.valid() else { return false }
        card.name = old.name; card.html = old.html; card.image = old.image
        card.contentWidth = old.width; card.contentHeight = old.height
        card.previous = nil; card.state = "{}"; card.revision = UUID(); card.edited = Date(); card.place()
        return true
    }
}
