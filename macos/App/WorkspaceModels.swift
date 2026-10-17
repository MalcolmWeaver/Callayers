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

    var width: Double { 640 * scale }
    var height: Double { 400 * scale }

    mutating func place(x: Double? = nil, y: Double? = nil, scale: Double? = nil) {
        if let scale = scale, scale.isFinite { self.scale = min(1.4, max(0.5, scale)) }
        if let x = x, x.isFinite { self.x = x }
        if let y = y, y.isFinite { self.y = y }
        self.x = min(max(0, self.x), 1280 - width)
        self.y = min(max(0, self.y), 720 - height)
    }
}

struct WorkspaceDocument: Codable, Equatable {
    var version = 1
    // Back to front. Library ordering is independent of the drawing order.
    var cards: [WorkspaceCard] = []
    var selectedID: UUID?

    func validated() throws -> WorkspaceDocument {
        guard version == 1 else { throw WorkspaceError.invalid("This workspace uses an unsupported version.") }
        guard cards.count <= 100, Set(cards.map(\.id)).count == cards.count,
              selectedID == nil || cards.contains(where: { $0.id == selectedID }) else {
            throw WorkspaceError.invalid("The saved card identities are invalid.")
        }
        for card in cards {
            guard !card.name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
                  card.name.utf16.count <= 120, card.html.utf8.count <= 131072,
                  card.edited.timeIntervalSince1970.isFinite,
                  card.x.isFinite, card.y.isFinite, card.scale.isFinite,
                  (0.5...1.4).contains(card.scale), card.x >= 0, card.y >= 0,
                  card.x + card.width <= 1280.001, card.y + card.height <= 720.001,
                  Self.validState(card.state) else {
                throw WorkspaceError.invalid("A saved card has invalid content, values, or placement.")
            }
        }
        return self
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
