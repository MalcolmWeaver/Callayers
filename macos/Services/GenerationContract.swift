import Foundation
import CoreFoundation

struct TextAttachment: Identifiable { let id = UUID(); let name: String; let text: String }
enum GenerationContract {
    static func body(prompt: String, attachments: [TextAttachment], current: WorkspaceCard?, id: UUID) throws -> [String:Any] {
        let prompt = prompt.trimmingCharacters(in:.whitespacesAndNewlines)
        guard !prompt.isEmpty || !attachments.isEmpty, attachments.count <= 8,
              attachments.allSatisfy({ !$0.name.isEmpty && $0.name.utf16.count <= 80 && !$0.text.trimmingCharacters(in:.whitespacesAndNewlines).isEmpty && !$0.name.unicodeScalars.contains(where: { $0.value < 32 }) }),
              prompt.utf8.count + attachments.reduce(0, { $0 + $1.text.utf8.count }) <= 100000 else { throw APIError(status:400,code:"prompt_too_large") }
        if attachments.isEmpty && prompt.range(of:"^(https?://\\S+|www\\.\\S+|[a-zA-Z0-9-]+\\.(com|org|net|io|co|edu|gov)(/\\S*)?)$",options:.regularExpression) != nil {
            throw APIError(status:400,code:"website_tools_not_ready")
        }
        var result: [String:Any] = ["prompt":prompt,"requestId":id.uuidString.lowercased()]
        if !attachments.isEmpty { result["attachments"] = attachments.map { ["name":$0.name,"text":$0.text] } }
        if let card = current {
            guard card.content.valid(), !card.html.lowercased().contains("data:image/") else { throw APIError(status:400,code:"invalid_revision") }
            result["currentCard"] = ["name":String(card.name.prefix(80)),"html":card.html,"width":Int(card.contentWidth),
                "height":Int(card.contentHeight),"imageIds":card.image == nil ? [] : ["local"]] as [String:Any]
        }
        return result
    }
    static func content(_ value: [String:Any], image: String?) throws -> CardContent {
        guard Set(value.keys) == Set(["name","html","width","height"]), let name = value["name"] as? String,
              !name.trimmingCharacters(in:.whitespacesAndNewlines).isEmpty, name.utf16.count <= 80,
              let html = value["html"] as? String, !html.trimmingCharacters(in:.whitespacesAndNewlines).isEmpty,
              let w = value["width"] as? NSNumber, let h = value["height"] as? NSNumber,
              CFGetTypeID(w) != CFBooleanGetTypeID(), CFGetTypeID(h) != CFBooleanGetTypeID(),
              w.doubleValue.rounded() == w.doubleValue, h.doubleValue.rounded() == h.doubleValue,
              (180...1280).contains(w.intValue), (100...720).contains(h.intValue) else { throw APIError(status:0,code:"invalid_card") }
        let result = CardContent(name:name.trimmingCharacters(in:.whitespacesAndNewlines),html:html,width:w.doubleValue,height:h.doubleValue,image:image)
        guard result.valid() else { throw APIError(status:0,code:"invalid_card") }; return result
    }
}
