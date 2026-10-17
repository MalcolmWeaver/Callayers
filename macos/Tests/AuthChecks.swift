import Foundation

private func require(_ condition: @autoclosure () -> Bool, _ message: String) throws {
    if !condition() { throw ServiceFailure(message: message) }
}
private func rejects(_ action: () throws -> Void) throws {
    var failed = false; do { try action() } catch { failed = true }
    try require(failed, "Invalid input was accepted")
}
private let configObject: [String: Any] = ["version": 2, "firebaseProjectId": "fixture", "googleDesktopClientId": "client", "googleDesktopClientSecret": "public", "firebaseApiKey": "key"]
private final class MemorySession: SessionStore {
    var value: SavedSession?
    var failWrite = false
    func load() throws -> SavedSession? { value }
    func save(_ session: SavedSession) throws { if failWrite { throw AuthFailure.storage }; value = session }
    func clear() throws { value = nil }
}
@MainActor
private final class FixtureAuth: AuthServicing {
    var pending: CheckedContinuation<AuthGrant, Error>?
    func config() async throws -> AuthConfig { try AuthConfig(service: ServiceClient.production, object: configObject) }
    func refresh(_ saved: SavedSession, config: AuthConfig) async throws -> AuthGrant { try await signIn(config: config) }
    func signIn(config: AuthConfig) async throws -> AuthGrant { try await withCheckedThrowingContinuation { pending = $0 } }
}
private final class FixtureHTTP: HTTPTransport {
    var requests: [URLRequest] = []
    var responses: [HTTPResult]
    init(_ objects: [[String: Any]]) throws { responses = try objects.map { HTTPResult(status: 200, body: try JSONSerialization.data(withJSONObject: $0)) } }
    func send(_ request: URLRequest, limit: Int) async throws -> HTTPResult {
        requests.append(request); guard !responses.isEmpty else { throw ServiceFailure.invalid }; return responses.removeFirst()
    }
}
@main
struct AuthChecks {
    @MainActor static func settle() async { for _ in 0..<15 { await Task.yield() } }
    @MainActor static func main() async throws {
        try require(OAuthAttempt.challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM", "PKCE RFC vector failed")
        let one = try OAuthAttempt(), two = try OAuthAttempt()
        try require(one.state != two.state && one.verifier.count == 43, "OAuth randomness failed")
        let code = try OAuthAttempt.callback("/callback?code=a%2Bb&state=right", state: "right")
        try require(code == "a+b", "Callback decoding failed")
        for target in ["/callback?code=x&state=wrong", "/callback?code=x&state=right&state=right", "/callback?code=x&code=y&state=right", "/callback?code=x%0A&state=right", "/callback?code=%ZZ&state=right", "/other?code=x&state=right", "/callback?code=x&error=denied&state=right"] {
            try rejects { _ = try OAuthAttempt.callback(target, state: "right") }
        }
        try rejects { _ = try ServiceClient.serviceURL("http://example.com") }
        try rejects { _ = try ServiceClient.serviceURL("https://user:pass@example.com") }
        print("PASS PKCE, random state, callback validation and service URL restrictions")

        let saved = SavedSession(service: ServiceClient.production, project: "fixture", uid: "uid", refresh: "refresh")
        let http = try FixtureHTTP([configObject, ["id_token": "token", "refresh_token": "rotated", "user_id": "uid", "expires_in": "3600"], ["uid": "uid", "accessAllowed": true, "email": "test@example.invalid"]])
        let service = AuthService(http: http)
        let config = try await service.config()
        let result = try await service.refresh(saved, config: config)
        try require(result.saved.refresh == "rotated" && result.saved.uid == "uid", "Refresh rotation/identity failed")
        try require(http.requests[2].value(forHTTPHeaderField: "Authorization") == "Bearer token", "Account was not authenticated")
        let mismatchHTTP = try FixtureHTTP([["id_token": "token", "refresh_token": "rotated", "user_id": "other", "expires_in": "3600"]])
        var rejected = false
        do { _ = try await AuthService(http: mismatchHTTP).refresh(saved, config: config) } catch { rejected = true }
        try require(rejected && mismatchHTTP.requests.count == 1, "A different account was accepted")
        print("PASS refresh exchange, rotated credential and verified account binding")

        let fixture = FixtureAuth(), store = MemorySession()
        let auth = AuthController(service: fixture, store: store, startTimer: false)
        var transitions: [Bool] = []; auth.onAccessChanged = { transitions.append($0) }
        auth.signIn(); await settle()
        guard let first = fixture.pending else { throw ServiceFailure.invalid }; fixture.pending = nil
        auth.cancel(); first.resume(returning: result); await settle()
        try require(!auth.allowed && store.value == nil, "Cancelled sign-in reopened access")
        auth.signIn(); await settle(); fixture.pending?.resume(returning: result); fixture.pending = nil; await settle()
        try require(auth.allowed && store.value == result.saved && transitions == [true], "Verified sign-in did not open access")
        auth.checkExpiry(now: result.expires.addingTimeInterval(1))
        try require(!auth.allowed && transitions == [true, false], "Expired sign-in did not stop access")
        auth.restore(); await settle()
        guard let late = fixture.pending else { throw ServiceFailure.invalid }; fixture.pending = nil
        auth.signOut(); late.resume(returning: result); await settle()
        try require(!auth.allowed && store.value == nil, "Late refresh restored a signed-out account")
        store.failWrite = true; auth.signIn(); await settle(); fixture.pending?.resume(returning: result); fixture.pending = nil; await settle()
        try require(!auth.allowed, "Keychain failure allowed an unsaved session")
        print("PASS cancellation, late refresh rejection, expiry gating, sign-out and secure-storage failure")

        if CommandLine.arguments.contains("--loopback") {
            let callback = LoopbackSignIn(), attempt = try OAuthAttempt()
            var httpTask: Task<Void, Never>?
            let (code, redirect) = try await callback.authorize(config: config, attempt: attempt) { url in
                let fields = URLComponents(url: url, resolvingAgainstBaseURL: false)!.queryItems!
                let redirect = fields.first { $0.name == "redirect_uri" }!.value!
                let state = fields.first { $0.name == "state" }!.value!
                httpTask = Task {
                    do {
                        let session = URLSession(configuration: .ephemeral)
                        let (_, invalid) = try await session.data(from: URL(string: redirect + "?code=bad&state=wrong")!)
                        try require((invalid as? HTTPURLResponse)?.statusCode == 400, "Listener accepted wrong state")
                        let (_, valid) = try await session.data(from: URL(string: redirect + "?code=fixture&state=" + state)!)
                        try require((valid as? HTTPURLResponse)?.statusCode == 200, "Listener did not acknowledge valid callback")
                    } catch { fputs("Loopback HTTP failed\n", stderr); exit(1) }
                }
                return true
            }
            callback.complete(success: true)
            await httpTask?.value
            try require(code == "fixture" && redirect.host == "127.0.0.1", "Loopback exchange failed")
            print("PASS actual loopback listener rejects wrong state and accepts one valid callback")
        }
    }
}
