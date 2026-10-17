import Foundation
@main struct GenerationChecks {
    static func main() throws {
        func check(_ b: Bool, _ m: String) throws { if !b { throw WorkspaceError.invalid(m) } }
        let good: [String:Any] = ["name":"Agenda","html":"<p>Hi</p>","width":480,"height":300]
        let content = try GenerationContract.content(good, image:nil)
        try check(content.width == 480 && content.name == "Agenda", "Valid response rejected")
        for input in ["https://example.com","www.example.com","example.com"] {
            do { _ = try GenerationContract.body(prompt:input, attachments:[], current:nil, id:UUID()); throw NSError(domain:"Bare URL accepted",code:1) }
            catch let error as APIError { try check(error.code == "website_tools_not_ready", "Wrong URL error") }
        }
        let body = try GenerationContract.body(prompt:"Make an agenda", attachments:[], current:nil, id:UUID())
        try check(body["requestId"] is String && body["currentCard"] == nil, "Create request shape wrong")
        var imageCard = WorkspaceCard(name:"Image",html:"<img src='callayers-image:local'>")
        imageCard.image = "data:image/png;base64,eA=="
        let revision = try GenerationContract.body(prompt:"Add a title", attachments:[], current:imageCard, id:UUID())
        let json = try JSONSerialization.data(withJSONObject:revision)
        try check(!String(decoding:json,as:UTF8.self).contains("base64"), "Image bytes sent to backend")
        print("PASS generation DTO, bare URL rejection, response validation and image privacy")
    }
}
