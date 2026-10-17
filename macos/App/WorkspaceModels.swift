import Foundation

enum CardKind: String, Codable { case ai, image }
struct CardContent: Codable, Equatable {
    var name: String
    var html: String
    var width: Double
    var height: Double
    var image: String? = nil
    func valid() -> Bool {
        guard !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty, name.utf16.count <= 120,
              !name.unicodeScalars.contains(where: { $0.value < 32 }), html.utf8.count <= 131072,
              width.isFinite, height.isFinite, (1...1280).contains(width), (1...720).contains(height) else { return false }
        if let image {
            guard html.contains("callayers-image:local"), image.hasPrefix("data:image/png;base64,"),
                  let data = Data(base64Encoded: String(image.dropFirst(22))), !data.isEmpty, data.count <= 1048576 else { return false }
        }
        return true
    }
}

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
    var contentWidth = 640.0
    var contentHeight = 400.0
    var image: String?
    var previous: CardContent?
    var revision = UUID()
    var renamed = false
    var kind: CardKind { image == nil ? .ai : .image }
    var width: Double { contentWidth * scale }
    var height: Double { contentHeight * scale }
    var maximumScale: Double { min(4, 1280 / contentWidth, 720 / contentHeight) }
    var content: CardContent { CardContent(name: name, html: html, width: contentWidth, height: contentHeight, image: image) }
    init(name: String, html: String) { self.name = name; self.html = html }
    mutating func place(x: Double? = nil, y: Double? = nil, scale: Double? = nil) {
        if let scale, scale.isFinite { self.scale = min(maximumScale, max(0.1, scale)) }
        self.scale = min(self.scale, maximumScale)
        if let x, x.isFinite { self.x = x }
        if let y, y.isFinite { self.y = y }
        self.x = min(max(0, self.x), max(0, 1280 - width))
        self.y = min(max(0, self.y), max(0, 720 - height))
    }
    enum CodingKeys: String, CodingKey { case id,name,html,state,visible,x,y,scale,edited,contentWidth,contentHeight,image,previous,revision,renamed }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decode(UUID.self, forKey: .id); name = try c.decode(String.self, forKey: .name)
        html = try c.decode(String.self, forKey: .html); state = try c.decode(String.self, forKey: .state)
        visible = try c.decode(Bool.self, forKey: .visible); x = try c.decode(Double.self, forKey: .x)
        y = try c.decode(Double.self, forKey: .y); scale = try c.decode(Double.self, forKey: .scale)
        edited = try c.decode(Date.self, forKey: .edited)
        contentWidth = try c.decodeIfPresent(Double.self, forKey: .contentWidth) ?? 640
        contentHeight = try c.decodeIfPresent(Double.self, forKey: .contentHeight) ?? 400
        image = try c.decodeIfPresent(String.self, forKey: .image); previous = try c.decodeIfPresent(CardContent.self, forKey: .previous)
        revision = try c.decodeIfPresent(UUID.self, forKey: .revision) ?? UUID()
        renamed = try c.decodeIfPresent(Bool.self, forKey: .renamed) ?? false
    }
}

struct WorkspaceDocument: Codable, Equatable {
    var version = 2
    // Back to front; sidebar sorting does not change composition order.
    var cards: [WorkspaceCard] = []
    var selectedID: UUID?
    var annotations: [Annotation] = []
    var prompt = ""
    init() {}
    enum CodingKeys: String, CodingKey { case version,cards,selectedID,annotations,prompt }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        version = try c.decode(Int.self, forKey: .version); cards = try c.decode([WorkspaceCard].self, forKey: .cards)
        selectedID = try c.decodeIfPresent(UUID.self, forKey: .selectedID)
        annotations = try c.decodeIfPresent([Annotation].self, forKey: .annotations) ?? []
        prompt = try c.decodeIfPresent(String.self, forKey: .prompt) ?? ""
    }
    func validated() throws -> WorkspaceDocument {
        guard (1...2).contains(version) else { throw WorkspaceError.invalid("This workspace uses an unsupported version.") }
        guard cards.count <= 100, Set(cards.map(\.id)).count == cards.count,
              selectedID == nil || cards.contains(where: { $0.id == selectedID }), prompt.utf8.count <= 100000,
              annotations.count <= 300, Set(annotations.map(\.id)).count == annotations.count,
              annotations.reduce(0, { $0 + $1.points.count }) <= 65536, annotations.allSatisfy({ $0.valid }) else {
            throw WorkspaceError.invalid("The saved workspace contains invalid cards or annotations.")
        }
        for card in cards {
            guard card.content.valid(), card.previous?.valid() ?? true, card.edited.timeIntervalSince1970.isFinite,
                  card.x.isFinite, card.y.isFinite, card.scale.isFinite, (0.1...4).contains(card.scale),
                  card.x >= 0, card.y >= 0, card.x + card.width <= 1280.001, card.y + card.height <= 720.001,
                  Self.validState(card.state) else { throw WorkspaceError.invalid("A saved card has invalid content, values, or placement.") }
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
        if let object = value as? [String: Any] { return object.count <= 64 && object.allSatisfy {
            $0.key.utf16.count <= 80 && !["__proto__", "constructor", "prototype"].contains($0.key) && validValue($0.value, depth: depth + 1)
        } }
        return false
    }
}

enum WorkspaceError: LocalizedError {
    case invalid(String), locked
    var errorDescription: String? {
        switch self {
        case .invalid(let message): return message
        case .locked: return "Your saved workspace could not be opened. It has been kept unchanged. Retry opening it or restore the saved backup."
        }
    }
}
