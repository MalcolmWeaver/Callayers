import Foundation

struct WorkspaceCard: Codable, Identifiable, Equatable {
    var id = UUID()
    var name: String
    var html: String
    var state = "{}"
    var visible = true
    var x = 160.0
    var y = 120.0
    var scale = 1.0
    var edited = Date()

    // Absent in the original workspace; legacy cards retain their 640×400 canvas.
    var contentWidth: Double?
    var contentHeight: Double?
    var imageID: UUID?
    var previousVersion: CardVersion?
    var contentRevision: UUID?
    var canvasWidth: Double { contentWidth ?? 640 }
    var canvasHeight: Double { contentHeight ?? 400 }
    var maximumScale: Double { min(4, min(1280 / canvasWidth, 720 / canvasHeight)) }
    var width: Double { canvasWidth * scale }
    var height: Double { canvasHeight * scale }

    mutating func place(x: Double? = nil, y: Double? = nil, scale: Double? = nil) {
        if let scale = scale, scale.isFinite { self.scale = min(maximumScale, max(0.1, scale)) }
        if let x = x, x.isFinite { self.x = x }
        if let y = y, y.isFinite { self.y = y }
        self.x = min(max(0, self.x), 1280 - width)
        self.y = min(max(0, self.y), 720 - height)
    }
}

struct CardVersion: Codable, Equatable {
    var name: String
    var html: String
    var width: Double
    var height: Double
    var imageID: UUID?
    var valid: Bool {
        !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && name.utf16.count <= 120 &&
        html.utf8.count <= 131072 && width.isFinite && height.isFinite && (1...1280).contains(width) && (1...720).contains(height)
    }
}

enum CardOrder { case front, forward, backward, back }

extension WorkspaceCard {
    var versionSnapshot: CardVersion {
        CardVersion(name: name, html: html, width: canvasWidth, height: canvasHeight, imageID: imageID)
    }
    mutating func revise(html: String, width: Double, height: Double, expectedRevision: UUID?) -> Bool {
        let candidate = CardVersion(name: name, html: html, width: width, height: height, imageID: imageID)
        guard contentRevision == expectedRevision, candidate.valid else { return false }
        previousVersion = versionSnapshot
        self.html = html; contentWidth = width; contentHeight = height
        state = "{}"; contentRevision = UUID(); edited = Date()
        place(scale: scale)
        return true
    }
    mutating func restorePreviousVersion() -> Bool {
        guard let previousVersion, previousVersion.valid else { return false }
        name = previousVersion.name; html = previousVersion.html
        contentWidth = previousVersion.width; contentHeight = previousVersion.height; imageID = previousVersion.imageID
        self.previousVersion = nil; state = "{}"; contentRevision = UUID(); edited = Date()
        place(scale: scale)
        return true
    }
}

struct WorkspaceDocument: Codable, Equatable {
    var version = 3
    // Back to front. Library ordering is independent of the drawing order.
    var cards: [WorkspaceCard] = []
    var selectedID: UUID?
    var promptDraft: String?
    var annotations: [WorkspaceAnnotation]?
    var annotationItems: [WorkspaceAnnotation] {
        get { annotations ?? [] }
        set { annotations = newValue }
    }

    func validated() throws -> WorkspaceDocument {
        guard (1...3).contains(version) else { throw WorkspaceError.invalid("This workspace uses an unsupported version.") }
        guard cards.count <= 100, Set(cards.map(\.id)).count == cards.count,
              selectedID == nil || cards.contains(where: { $0.id == selectedID }) else {
            throw WorkspaceError.invalid("The saved card identities are invalid.")
        }
        for card in cards {
            guard !card.name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
                  card.name.utf16.count <= 120, card.html.utf8.count <= 131072,
                  card.edited.timeIntervalSince1970.isFinite,
                  card.x.isFinite, card.y.isFinite, card.scale.isFinite,
                  (1...1280).contains(card.canvasWidth), (1...720).contains(card.canvasHeight),
                  (0.1...card.maximumScale).contains(card.scale), card.x >= 0, card.y >= 0,
                  card.x + card.width <= 1280.001, card.y + card.height <= 720.001,
                  card.previousVersion?.valid ?? true,
                  Self.validState(card.state) else {
                throw WorkspaceError.invalid("A saved card has invalid content, values, or placement.")
            }
        }
        guard (promptDraft?.utf8.count ?? 0) <= 100_000 else { throw WorkspaceError.invalid("The prompt draft is too large.") }
        guard annotationItems.count <= 500, Set(annotationItems.map(\.id)).count == annotationItems.count,
              annotationItems.allSatisfy(\.valid), annotationItems.reduce(0, { $0 + $1.points.count }) <= 200_000 else {
            throw WorkspaceError.invalid("The saved annotations are invalid or too large.")
        }
        var upgraded = self; upgraded.version = 3
        return upgraded
    }

    mutating func reorder(_ id: UUID, _ order: CardOrder) {
        guard let index = cards.firstIndex(where: { $0.id == id }) else { return }
        let destination: Int
        switch order {
        case .front: destination = cards.count - 1
        case .forward: destination = min(cards.count - 1, index + 1)
        case .backward: destination = max(0, index - 1)
        case .back: destination = 0
        }
        let card = cards.remove(at: index); cards.insert(card, at: destination)
    }

    static func validState(_ text: String) -> Bool {
        guard text.utf8.count <= 16384, let data = text.data(using: .utf8),
              let object = try? JSONSerialization.jsonObject(with: data), object is [String: Any] else { return false }
        return validValue(object, depth: 0)
    }

    private static func validValue(_ value: Any, depth: Int) -> Bool {
        guard depth <= 4 else { return false }
        if value is NSNull { return true }
        if let text = value as? String { return text.utf16.count <= 4096 }
        if let number = value as? NSNumber { return number.doubleValue.isFinite }
        if let array = value as? [Any] { return array.count <= 128 && array.allSatisfy { validValue($0, depth: depth + 1) } }
        if let object = value as? [String: Any] {
            return object.count <= 64 && object.allSatisfy {
                $0.key.utf16.count <= 80 && !["__proto__", "constructor", "prototype"].contains($0.key) && validValue($0.value, depth: depth + 1)
            }
        }
        return false
    }
}

enum WorkspaceError: LocalizedError {
    case invalid(String)
    case locked
    var errorDescription: String? {
        switch self {
        case .invalid(let message): return message
        case .locked: return "Your saved workspace could not be opened. It has been kept unchanged. Retry opening it or restore the saved backup."
        }
    }
}
