import Foundation
import CoreFoundation

struct GeneratedContent: Equatable {
    let name: String
    let html: String
    let width: Double
    let height: Double
    init(_ object: [String: Any], imageRequired: Bool) throws {
        guard Set(object.keys) == Set(["name", "html", "width", "height"]) else { throw ServiceFailure.invalid }
        name = try ServiceClient.string(object, "name", max: 320).trimmingCharacters(in: .whitespacesAndNewlines)
        guard !name.isEmpty, name.utf16.count <= 80, let html = object["html"] as? String,
              !html.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty, html.utf8.count <= 131072 else { throw ServiceFailure.invalid }
        func dimension(_ key: String, _ range: ClosedRange<Double>) throws -> Double {
            guard let value = object[key] as? NSNumber, CFGetTypeID(value) != CFBooleanGetTypeID(),
                  value.doubleValue.isFinite, range.contains(value.doubleValue), value.doubleValue.rounded() == value.doubleValue else { throw ServiceFailure.invalid }
            return value.doubleValue
        }
        width = try dimension("width", 180...1280); height = try dimension("height", 100...720)
        let hasLocal = html.contains("callayers-image:local")
        guard hasLocal == imageRequired,
              !html.replacingOccurrences(of: "callayers-image:local", with: "").contains("callayers-image:"),
              html.range(of: "data:image/", options: .caseInsensitive) == nil else { throw ServiceFailure.invalid }
        self.html = html
    }
}
struct GenerationTicket {
    let id = UUID()
    let target: UUID?
    let revision: UUID?
    init(card: WorkspaceCard?) { target = card?.id; revision = card?.contentRevision }
}
struct GenerationFailure: LocalizedError {
    let code: String
    let status: Int
    var errorDescription: String? {
        switch code {
        case "daily_limit_reached": return "You've reached today's AI limit. It resets at midnight UTC."
        case "website_tools_not_ready": return "Paste the page text here. Callayers can't open website links yet."
        case "prompt_too_large": return "Use up to 100 KB of text. Your draft has been kept."
        case "invalid_request": return "Enter a description of the card you want."
        default:
            if status == 401 || status == 403 { return "Sign in again to continue." }
            return "The response couldn't be received or validated. Your existing card was kept. Sending again starts a new generation and may use another allowance unit."
        }
    }
}
struct GenerationService {
    let http: HTTPTransport
    let service: URL
    init(http: HTTPTransport = ServiceClient(), service: URL = ServiceClient.production) { self.http = http; self.service = service }
    static func body(prompt: String, card: WorkspaceCard?, ticket: GenerationTicket) throws -> Data {
        guard prompt.utf8.count <= 100_000 else { throw GenerationFailure(code: "prompt_too_large", status: 413) }
        guard !prompt.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { throw GenerationFailure(code: "invalid_request", status: 400) }
        let trimmed = prompt.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.range(of: "^(https?://\\S+|www\\.\\S+|[a-z0-9-]+\\.(com|org|net|io|co|edu|gov)(/\\S*)?)$", options: [.regularExpression, .caseInsensitive]) != nil {
            throw GenerationFailure(code: "website_tools_not_ready", status: 400)
        }
        var body: [String: Any] = ["requestId": ticket.id.uuidString.lowercased(), "prompt": prompt, "attachments": []]
        if let card {
            guard card.html.utf8.count <= 131072, card.html.range(of: "data:image/", options: .caseInsensitive) == nil,
                  card.imageID == nil || card.html.contains("callayers-image:local") else { throw ServiceFailure.invalid }
            var name = card.name.filter { !$0.unicodeScalars.contains(where: { $0.value < 32 || $0.value == 127 }) }
            while name.utf16.count > 80 { name.removeLast() }
            if name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty { name = "Card" }
            body["currentCard"] = ["name": name, "html": card.html, "width": card.canvasWidth,
                                   "height": card.canvasHeight, "imageIds": card.imageID == nil ? [] : ["local"]] as [String: Any]
        }
        return try JSONSerialization.data(withJSONObject: body)
    }
    func generate(prompt: String, card: WorkspaceCard?, ticket: GenerationTicket, token: String) async throws -> GeneratedContent {
        let body = try Self.body(prompt: prompt, card: card, ticket: ticket)
        let response: HTTPResult
        do {
            response = try await http.send(ServiceClient.request(service.appendingPathComponent("generate"), method: "POST", body: body, token: token, timeout: 120), limit: 1_048_576)
        } catch is CancellationError { throw CancellationError() }
        catch { throw GenerationFailure(code: "response_lost", status: 0) }
        do {
            let object = try response.object()
            guard response.status == 200 else { throw GenerationFailure(code: object["error"] as? String ?? "unavailable", status: response.status) }
            guard object["requestId"] as? String == ticket.id.uuidString.lowercased(), let cardObject = object["card"] as? [String: Any] else { throw ServiceFailure.invalid }
            return try GeneratedContent(cardObject, imageRequired: card?.imageID != nil)
        } catch let failure as GenerationFailure { throw failure }
        catch { throw GenerationFailure(code: "invalid_response", status: response.status) }
    }
}
extension WorkspaceDocument {
    @discardableResult mutating func apply(_ content: GeneratedContent, ticket: GenerationTicket) throws -> UUID {
        if let id = ticket.target {
            guard let index = cards.firstIndex(where: { $0.id == id }),
                  cards[index].revise(html: content.html, width: content.width, height: content.height, expectedRevision: ticket.revision) else {
                throw ServiceFailure(message: "The original card changed or was deleted. This response was discarded.")
            }
            return id
        }
        guard cards.count < 100 else { throw ServiceFailure(message: "Your library is full. Remove a card before creating another.") }
        var card = WorkspaceCard(name: content.name, html: content.html)
        card.visible = false
        card.contentWidth = content.width; card.contentHeight = content.height; card.contentRevision = UUID()
        card.place(x: 48, y: 48)
        cards.append(card); selectedID = card.id
        return card.id
    }
}
