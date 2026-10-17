import Foundation

@MainActor final class Telemetry {
    private var queue = TelemetryQueue()
    private var session = UUID()
    private var account = ""
    private var epoch: UUID?
    private var video: VideoTelemetryState?
    private var previewCards: [UUID:CardKind] = [:]
    private var outputCards: [UUID:CardKind] = [:]
    private var nextFlush = Date.distantFuture
    private var operation: Task<Void,Never>?
    private var operationID = UUID()
    func observe(auth: AuthController,state: VideoTelemetryState,preview: [UUID:CardKind],output: [UUID:CardKind]) {
        guard auth.allowed else { end(); return }
        if account != auth.accountID || epoch != auth.epoch {
            end(); account = auth.accountID; epoch = auth.epoch; session = UUID()
            emit("session_started",[:])
        }
        if video != state { emit("video_state_changed",state.properties); video = state }
        usage(old:previewCards,new:preview,destination:"preview"); usage(old:outputCards,new:output,destination:"output")
        previewCards = preview; outputCards = output
        if Date() >= nextFlush || queue.events.count >= 32 { flush(auth) }
    }
    func created(kind: String,id: UUID?) {
        guard !account.isEmpty else { return }
        if kind == "annotation" { emit("content_created",["kind":kind]) }
        else if ["ai_create","ai_revision","image_import"].contains(kind),let id { emit("content_created",["kind":kind,"card_id":id.uuidString.lowercased()]) }
    }
    private func usage(old: [UUID:CardKind],new: [UUID:CardKind],destination: String) {
        for (id,kind) in old where new[id] != kind { emit("card_usage_changed",["card_id":id.uuidString.lowercased(),"card_type":kind.rawValue,"destination":destination,"applied":false]) }
        for (id,kind) in new where old[id] != kind { emit("card_usage_changed",["card_id":id.uuidString.lowercased(),"card_type":kind.rawValue,"destination":destination,"applied":true]) }
    }
    private func emit(_ name: String,_ properties: [String:Any]) {
        queue.append(TelemetryEvent(name:name,properties:properties,session:session,date:Date()))
        if nextFlush == .distantFuture { nextFlush = Date().addingTimeInterval(2) }
    }
    private func flush(_ auth: AuthController) {
        guard operation == nil else { return }; queue.expire(now:Date())
        let batch = queue.batch(now:Date()); guard !batch.isEmpty else { nextFlush = .distantFuture; return }
        let expectedAccount = account, expectedEpoch = epoch
        let id = UUID(); operationID = id
        nextFlush = Date().addingTimeInterval(2)
        operation = Task { [weak self,weak auth] in
            guard let self,let auth else { return }
            defer { if operationID == id { operation = nil } }
            for attempt in 0..<3 {
                guard !Task.isCancelled, auth.allowed, auth.accountID == expectedAccount, auth.epoch == expectedEpoch,
                      batch.allSatisfy({ Date().timeIntervalSince($0.date) <= 300 }) else { return }
                do {
                    let token = try await auth.validToken()
                    guard auth.accountID == expectedAccount,auth.epoch == expectedEpoch else { return }
                    _ = try await auth.client.request("events",method:"POST",body:["events":batch.map(\.json)],token:token)
                    queue.remove(Set(batch.map(\.id))); return
                } catch {
                    if attempt < 2 { do { try await Task.sleep(nanoseconds:attempt == 0 ? 2_000_000_000 : 10_000_000_000) } catch { return } }
                }
            }
            queue.remove(Set(batch.map(\.id)))
        }
    }
    func finish(auth: AuthController) async {
        usage(old:previewCards,new:[:],destination:"preview"); usage(old:outputCards,new:[:],destination:"output")
        previewCards = [:]; outputCards = [:]
        flush(auth)
        for _ in 0..<40 { if operation == nil { break }; try? await Task.sleep(nanoseconds:50_000_000) }
    }
    func end() {
        operationID = UUID(); operation?.cancel(); operation = nil; queue = TelemetryQueue(); account = ""; epoch = nil
        previewCards = [:]; outputCards = [:]; video = nil; nextFlush = .distantFuture
    }
}
