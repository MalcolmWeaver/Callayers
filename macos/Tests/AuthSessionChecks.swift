import Foundation
import AppKit
final class FakeSessionStorage: SessionStorage {
    var value: SavedSession?
    var failSave = false
    func load() throws -> SavedSession? { value }
    func save(_ session: SavedSession) throws { if failSave { throw NSError(domain:"locked",code:1) }; value = session }
    func clear() throws { value = nil }
}
final class AuthFixtureProtocol: URLProtocol {
    static var wrongUser = false
    static var status = 200
    private var stopped = false
    private let lock = NSLock()
    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func startLoading() {
        let path = request.url!.path
        let json: [String:Any]
        if path == "/client-config" { json = ["version":2,"firebaseProjectId":"test-project","googleDesktopClientId":"client","googleDesktopClientSecret":"installed-public-config","firebaseApiKey":"api"] }
        else if path == "/v1/token" { json = ["id_token":"test-token","refresh_token":"new-refresh","user_id":Self.wrongUser ? "other" : "user","expires_in":"3600"] }
        else if path == "/generate" {
            var body = request.httpBody ?? Data()
            if body.isEmpty,let stream = request.httpBodyStream {
                stream.open(); defer { stream.close() }
                var bytes = [UInt8](repeating:0,count:4096)
                while body.count < 800000 { let count = stream.read(&bytes,maxLength:bytes.count); if count <= 0 { break }; body.append(contentsOf:bytes.prefix(count)) }
            }
            let input = (try? JSONSerialization.jsonObject(with:body)) as? [String:Any] ?? [:]
            json = ["requestId":input["requestId"] as? String ?? "", "card":["name":"Generated","html":"<p>Generated</p>","width":480,"height":300]]
        }
        else { json = ["uid":"user","email":"test@example.com","accessAllowed":true,"allowance":["enabled":true,"remaining":4]] }
        let response = HTTPURLResponse(url:request.url!,statusCode:Self.status,httpVersion:nil,headerFields:["Content-Type":"application/json"])!
        let deliver = {
            self.lock.lock(); let canceled = self.stopped; self.lock.unlock(); guard !canceled else { return }
            self.client?.urlProtocol(self,didReceive:response,cacheStoragePolicy:.notAllowed)
            self.client?.urlProtocol(self,didLoad:try! JSONSerialization.data(withJSONObject:json)); self.client?.urlProtocolDidFinishLoading(self)
        }
        if path == "/generate" { DispatchQueue.global().asyncAfter(deadline:.now()+0.2,execute:deliver) } else { deliver() }
    }
    override func stopLoading() { lock.lock(); stopped = true; lock.unlock() }
}
@main struct AuthSessionChecks {
    @MainActor static func main() async throws {
        func check(_ b: Bool,_ m: String) throws { if !b { throw NSError(domain:m,code:1) } }
        let config = URLSessionConfiguration.ephemeral; config.protocolClasses = [AuthFixtureProtocol.self]
        let client = BackendClient(root:URL(string:"https://example.com")!,session:URLSession(configuration:config))
        let storage = FakeSessionStorage()
        storage.value = SavedSession(service:client.root.absoluteString,project:"test-project",uid:"user",refresh:"old-refresh")
        let auth = AuthController(client:client,storage:storage)
        auth.restore()
        for _ in 0..<200 { if !auth.busy { break }; try await Task.sleep(nanoseconds:10_000_000) }
        try check(auth.allowed && auth.accountID == "user" && storage.value?.refresh == "new-refresh", "Valid session did not restore")
        let generation = GenerationController()
        var applied = 0
        generation.start(prompt:"Make an agenda",attachments:[],card:nil,auth:auth) { _,_,_,_ in applied += 1; return true }
        for _ in 0..<200 { if !generation.busy { break }; try await Task.sleep(nanoseconds:10_000_000) }
        try check(applied == 1,"Valid generation did not apply: " + generation.message)
        var pendingApply: CheckedContinuation<Bool,Never>?
        var queuedPermit: GenerationPermit?
        generation.start(prompt:"Wait for save",attachments:[],card:nil,auth:auth) { _,_,_,permit in
            queuedPermit = permit
            return await withCheckedContinuation { pendingApply = $0 }
        }
        for _ in 0..<200 { if pendingApply != nil { break }; try await Task.sleep(nanoseconds:10_000_000) }
        try check(pendingApply != nil,"Delayed apply did not start")
        generation.cancel()
        generation.start(prompt:"New request",attachments:[],card:nil,auth:auth) { _,_,_,_ in true }
        try check(queuedPermit?.valid == false,"Cancellation did not invalidate queued save permission")
        pendingApply?.resume(returning:false)
        try await Task.sleep(nanoseconds:30_000_000)
        try check(generation.message == "Creating your card…","Canceled apply overwrote a newer request status")
        for _ in 0..<200 { if !generation.busy { break }; try await Task.sleep(nanoseconds:10_000_000) }
        generation.start(prompt:"Another card",attachments:[],card:nil,auth:auth) { _,_,_,_ in applied += 1; return true }
        try await Task.sleep(nanoseconds:50_000_000); generation.cancel()
        try await Task.sleep(nanoseconds:300_000_000)
        try check(applied == 1,"Canceled generation changed content")
        generation.start(prompt:"Another card",attachments:[],card:nil,auth:auth) { _,_,_,_ in applied += 1; return true }
        try await Task.sleep(nanoseconds:50_000_000)
        auth.signOut()
        try await Task.sleep(nanoseconds:300_000_000)
        try check(applied == 1,"Old-account generation changed content")
        try check(!auth.allowed && storage.value == nil, "Sign out did not clear session")
        AuthFixtureProtocol.wrongUser = true
        storage.value = SavedSession(service:client.root.absoluteString,project:"test-project",uid:"user",refresh:"old")
        auth.restore()
        for _ in 0..<200 { if !auth.busy { break }; try await Task.sleep(nanoseconds:10_000_000) }
        try check(!auth.allowed && storage.value == nil, "Wrong-user renewal unlocked workspace")
        AuthFixtureProtocol.wrongUser = false; storage.failSave = true
        storage.value = SavedSession(service:client.root.absoluteString,project:"test-project",uid:"user",refresh:"old")
        auth.restore()
        for _ in 0..<200 { if !auth.busy { break }; try await Task.sleep(nanoseconds:10_000_000) }
        try check(!auth.allowed, "Keychain write failure unlocked workspace")
        print("PASS real async auth protocol with injected transport: restore, refresh binding, sign-out and locked storage")
    }
}
