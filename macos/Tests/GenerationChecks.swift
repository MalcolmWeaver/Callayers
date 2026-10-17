import Foundation

private func require(_ condition: @autoclosure () -> Bool, _ message: String) throws {
    if !condition() { throw ServiceFailure(message: message) }
}
private func rejects(_ action: () throws -> Void) throws {
    var rejected = false; do { try action() } catch { rejected = true }; try require(rejected, "Invalid generation was accepted")
}
private final class GenerationHTTP: HTTPTransport {
    var count = 0
    var response: HTTPResult?
    var lastRequest: URLRequest?
    func send(_ request: URLRequest, limit: Int) async throws -> HTTPResult {
        count += 1; lastRequest = request
        guard let response else { throw URLError(.networkConnectionLost) }; return response
    }
}
@main
struct GenerationChecks {
    static func main() async throws {
        let plain: [String: Any] = ["name": "Agenda", "html": "<p>Agenda</p>", "width": 640, "height": 400]
        let generated = try GeneratedContent(plain, imageRequired: false)
        var document = WorkspaceDocument()
        let create = GenerationTicket(card: nil)
        let id = try document.apply(generated, ticket: create)
        try require(document.cards.count == 1 && document.selectedID == id && document.cards[0].visible, "Creation lost identity or visibility")
        document.cards[0].name = "My name"; document.cards[0].state = "{\"count\":2}"
        let original = document.cards[0], revision = GenerationTicket(card: original)
        let newer = try GeneratedContent(["name": "AI name", "html": "<p>Changed</p>", "width": 500, "height": 300], imageRequired: false)
        try document.apply(newer, ticket: revision)
        try require(document.cards[0].id == id && document.cards[0].name == "My name" && document.cards[0].previousVersion == original.versionSnapshot && document.cards[0].state == "{}", "Revision lost name, identity, backup or state reset")
        let successful = document
        try rejects { try document.apply(generated, ticket: revision) }
        try require(document == successful, "Late revision changed the current or previous version")
        let deleted = GenerationTicket(card: document.cards[0]); document.cards = []; document.selectedID = nil
        try rejects { try document.apply(newer, ticket: deleted) }
        print("PASS create/revise, local names, one backup and stale/deleted target rejection")

        var image = WorkspaceCard(name: "local filename.png", html: "<img src=\"callayers-image:local\">")
        image.imageID = UUID()
        let imageTicket = GenerationTicket(card: image)
        let body = try GenerationService.body(prompt: "Add a caption", card: image, ticket: imageTicket)
        let object = try JSONSerialization.jsonObject(with: body) as! [String: Any]
        let current = object["currentCard"] as! [String: Any]
        try require(current["imageIds"] as? [String] == ["local"] && !String(data: body, encoding: .utf8)!.contains(image.imageID!.uuidString), "Local asset identity leaked into generation")
        var invalid = plain
        invalid["html"] = "<img src=\"callayers-image:other\">"
        try rejects { _ = try GeneratedContent(invalid, imageRequired: true) }
        try rejects { _ = try GeneratedContent(plain, imageRequired: true) }
        invalid = plain; invalid["html"] = "<img src=\"data:image/png;base64,secret\">"
        try rejects { _ = try GeneratedContent(invalid, imageRequired: false) }
        invalid = plain; invalid["width"] = 640.5
        try rejects { _ = try GeneratedContent(invalid, imageRequired: false) }
        invalid = plain; invalid["height"] = true
        try rejects { _ = try GeneratedContent(invalid, imageRequired: false) }
        try rejects { _ = try GenerationService.body(prompt: String(repeating: "x", count: 100001), card: nil, ticket: create) }
        try rejects { _ = try GenerationService.body(prompt: "https://example.com", card: nil, ticket: create) }
        print("PASS bounded prompts/dimensions, local-only image references and website guidance")

        let http = GenerationHTTP(), service = GenerationService(http: http)
        http.response = HTTPResult(status: 200, body: try JSONSerialization.data(withJSONObject: ["requestId": create.id.uuidString.lowercased(), "card": plain]))
        let received = try await service.generate(prompt: "Agenda", card: nil, ticket: create, token: "fixture")
        try require(received == generated && http.count == 1, "Generation response did not match")
        try require(http.lastRequest?.value(forHTTPHeaderField: "Authorization") == "Bearer fixture", "Generation was unauthenticated")
        http.response = nil
        do { _ = try await service.generate(prompt: "Agenda", card: nil, ticket: create, token: "fixture"); throw ServiceFailure.invalid }
        catch let failure as GenerationFailure { try require(failure.code == "response_lost" && http.count == 2, "Lost response automatically regenerated") }
        http.response = HTTPResult(status: 200, body: try JSONSerialization.data(withJSONObject: ["requestId": UUID().uuidString.lowercased(), "card": plain]))
        do { _ = try await service.generate(prompt: "Agenda", card: nil, ticket: create, token: "fixture"); throw ServiceFailure.invalid }
        catch let failure as GenerationFailure { try require(failure.code == "invalid_response", "Mismatched response was accepted") }
        http.response = HTTPResult(status: 429, body: Data("{\"error\":\"daily_limit_reached\"}".utf8))
        do { _ = try await service.generate(prompt: "Agenda", card: nil, ticket: create, token: "fixture"); throw ServiceFailure.invalid }
        catch let failure as GenerationFailure { try require(failure.code == "daily_limit_reached", "Allowance error lost") }
        print("PASS authenticated exchange, response identity, allowance handling and no automatic regeneration")
    }
}
