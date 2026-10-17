import Foundation
import Combine

@MainActor final class GenerationController: ObservableObject {
    @Published private(set) var busy = false
    @Published private(set) var message = ""
    private var operation: Task<Void,Never>?
    private var requestID: UUID?
    func cancel() { let hadRequest = requestID != nil; requestID = nil; operation?.cancel(); operation = nil; busy = false; if hadRequest { message = "Request canceled. Existing content is unchanged." } }
    func start(prompt: String, attachments: [TextAttachment], card: WorkspaceCard?, auth: AuthController,
               apply: @escaping (UUID?,UUID?,CardContent) async -> Bool) {
        guard !busy else { return }
        let id = UUID(), epoch = auth.epoch
        let body: [String:Any]
        do { body = try GenerationContract.body(prompt:prompt,attachments:attachments,current:card,id:id) }
        catch { message = error.localizedDescription; return }
        requestID = id; busy = true; message = card == nil ? "Creating your card…" : "Updating your card…"
        operation = Task { [weak self] in
            guard let self else { return }
            defer { if requestID == id { busy = false; operation = nil; requestID = nil } }
            do {
                let token = try await auth.validToken()
                let value = try await auth.client.request("generate",method:"POST",body:body,token:token)
                try Task.checkCancellation()
                guard requestID == id, auth.epoch == epoch, auth.allowed,
                      value["requestId"] as? String == id.uuidString.lowercased(), let object = value["card"] as? [String:Any] else { return }
                let content = try GenerationContract.content(object,image:card?.image)
                if let allowance = value["allowance"] as? [String:Any] { auth.allowance = AIAllowance(allowance) }
                message = await apply(card?.id,card?.revision,content) ? "" : "The card changed while this request was running. The result was discarded."
            } catch {
                guard requestID == id, auth.epoch == epoch else { return }
                if let failure = error as? APIError {
                    if failure.status == 401 || failure.status == 403 { auth.rejectSession() }
                    if failure.code == "daily_limit_reached" { auth.allowance.remaining = 0; auth.allowance.date = Self.utcToday() }
                }
                if !(error is CancellationError) { message = error is APIError ? error.localizedDescription : "The connection ended before your card was received. A new request uses another allowance unit." }
            }
        }
    }
    private static func utcToday() -> String {
        let formatter = DateFormatter(); formatter.timeZone = TimeZone(secondsFromGMT:0); formatter.dateFormat = "yyyy-MM-dd"; return formatter.string(from:Date())
    }
}
