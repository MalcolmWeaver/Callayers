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
    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func startLoading() {
        let path = request.url!.path
        let json: [String:Any]
        if path == "/client-config" { json = ["version":2,"firebaseProjectId":"test-project","googleDesktopClientId":"client","googleDesktopClientSecret":"installed-public-config","firebaseApiKey":"api"] }
        else if path == "/v1/token" { json = ["id_token":"test-token","refresh_token":"new-refresh","user_id":Self.wrongUser ? "other" : "user","expires_in":"3600"] }
        else { json = ["uid":"user","email":"test@example.com","accessAllowed":true,"allowance":["enabled":true,"remaining":4]] }
        let response = HTTPURLResponse(url:request.url!,statusCode:Self.status,httpVersion:nil,headerFields:["Content-Type":"application/json"])!
        client?.urlProtocol(self,didReceive:response,cacheStoragePolicy:.notAllowed)
        client?.urlProtocol(self,didLoad:try! JSONSerialization.data(withJSONObject:json)); client?.urlProtocolDidFinishLoading(self)
    }
    override func stopLoading() { }
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
        auth.signOut(); try check(!auth.allowed && storage.value == nil, "Sign out did not clear session")
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
