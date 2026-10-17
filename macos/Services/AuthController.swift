import AppKit
import Combine

struct AIAllowance {
    var enabled = true
    var remaining: Int?
    var date = ""
    init(_ json: [String: Any] = [:]) {
        enabled = json["enabled"] as? Bool ?? true; remaining = json["remaining"] as? Int; date = json["date"] as? String ?? ""
    }
    var exhausted: Bool {
        let formatter = DateFormatter(); formatter.dateFormat = "yyyy-MM-dd"; formatter.timeZone = TimeZone(secondsFromGMT: 0)
        return remaining == 0 && date == formatter.string(from: Date())
    }
}

@MainActor final class AuthController: ObservableObject {
    @Published private(set) var allowed = false
    @Published private(set) var busy = false
    @Published private(set) var email = ""
    @Published private(set) var message = "Sign in to use Callayers."
    @Published private(set) var accountID = ""
    @Published var allowance = AIAllowance()
    let client: BackendClient
    private let storage: SessionStorage
    private var config: [String: Any] = [:]
    private var saved: SavedSession?
    private var token = ""
    private var expiry = Date.distantPast
    private var operation: Task<Void, Never>?
    private var renewal: Task<String, Error>?
    private var callback: AuthCallback?
    private(set) var epoch = UUID()
    init(client: BackendClient = BackendClient(), storage: SessionStorage = SessionKeychain()) {
        self.client = client; self.storage = storage
    }
    private func required(_ json: [String: Any], _ key: String, max: Int = 16384) throws -> String {
        guard let value = json[key] as? String, !value.isEmpty, value.utf8.count <= max else { throw APIError(status: 0, code: "invalid_response") }
        return value
    }
    private func loadConfig() async throws {
        let value = try await client.request("client-config")
        guard value["version"] as? Int == 2 else { throw APIError(status: 0, code: "invalid_config") }
        for key in ["firebaseProjectId","googleDesktopClientId","googleDesktopClientSecret","firebaseApiKey"] { _ = try required(value, key, max: 256) }
        config = value
    }
    func restore() {
        guard operation == nil else { return }
        let generation = epoch; busy = true; message = "Checking your sign-in…"
        operation = Task { [weak self] in
            guard let self else { return }
            defer { if self.epoch == generation { self.busy = false; self.operation = nil } }
            do {
                guard let session = try storage.load() else { message = "Sign in to use Callayers."; return }
                try await loadConfig(); try Task.checkCancellation()
                guard epoch == generation else { return }
                saved = session
                _ = try await renew(expected: generation)
            } catch { if epoch == generation { handle(error) } }
        }
    }
    func signIn() {
        cancel(); busy = true; message = "Opening Google sign-in…"
        let generation = epoch
        operation = Task { [weak self] in
            guard let self else { return }
            defer { if epoch == generation { busy = false; operation = nil; callback = nil } }
            do {
                try await loadConfig(); try Task.checkCancellation()
                let state = try AuthCallback.random(32), verifier = try AuthCallback.random(48)
                let listener = AuthCallback(); callback = listener
                let port = try await listener.start(state: state)
                let redirect = "http://127.0.0.1:\(port)/callback"
                var url = URLComponents(string: "https://accounts.google.com/o/oauth2/v2/auth")!
                url.queryItems = ["client_id": try required(config, "googleDesktopClientId"), "redirect_uri": redirect,
                    "response_type": "code", "scope": "openid email profile", "state": state,
                    "code_challenge": AuthCallback.challenge(verifier), "code_challenge_method": "S256", "prompt": "select_account"]
                    .map { URLQueryItem(name: $0.key, value: $0.value) }
                guard epoch == generation, let authorizationURL = url.url else { throw CancellationError() }
                message = "Finish signing in with Google in your browser."
                guard NSWorkspace.shared.open(authorizationURL) else { throw APIError(status: 0, code: "browser_unavailable") }
                let code = try await listener.wait(); try Task.checkCancellation()
                let google = try await client.send(url: URL(string: "https://oauth2.googleapis.com/token")!, method: "POST",
                    data: BackendClient.form(["client_id": try required(config,"googleDesktopClientId"),
                        "client_secret": try required(config,"googleDesktopClientSecret"), "code": code,
                        "redirect_uri": redirect, "grant_type": "authorization_code", "code_verifier": verifier]),
                    type: "application/x-www-form-urlencoded")
                var firebaseURL = URLComponents(string: "https://identitytoolkit.googleapis.com/v1/accounts:signInWithIdp")!
                firebaseURL.queryItems = [URLQueryItem(name: "key", value: try required(config,"firebaseApiKey"))]
                let body: [String: Any] = ["requestUri": redirect, "returnSecureToken": true,
                    "postBody": String(data: BackendClient.form(["id_token": try required(google,"id_token"), "providerId": "google.com"]), encoding: .utf8)!]
                let firebase = try await client.send(url: firebaseURL.url!, method: "POST", data: JSONSerialization.data(withJSONObject: body))
                guard epoch == generation else { throw CancellationError() }
                saved = SavedSession(service: client.root.absoluteString, project: try required(config,"firebaseProjectId"),
                    uid: try required(firebase,"localId", max:128), refresh: try required(firebase,"refreshToken"))
                _ = try await renew(expected: generation)
                NSApp.activate(ignoringOtherApps: true)
            } catch { if epoch == generation { callback?.cancel(); handle(error) } }
        }
    }
    private func renew(expected: UUID) async throws -> String {
        guard let saved, saved.service == client.root.absoluteString, saved.project == (config["firebaseProjectId"] as? String) else {
            throw APIError(status: 401, code: "invalid_session")
        }
        var url = URLComponents(string: "https://securetoken.googleapis.com/v1/token")!
        url.queryItems = [URLQueryItem(name:"key", value: try required(config,"firebaseApiKey"))]
        let value = try await client.send(url: url.url!, method: "POST", data: BackendClient.form(["grant_type":"refresh_token","refresh_token":saved.refresh]), type: "application/x-www-form-urlencoded")
        let idToken = try required(value,"id_token")
        guard try required(value,"user_id", max:128) == saved.uid,
              let seconds = Double(try required(value,"expires_in", max:10)), (60...86400).contains(seconds) else { throw APIError(status:401,code:"invalid_session") }
        let account = try await client.request("account", token: idToken)
        guard account["uid"] as? String == saved.uid, account["accessAllowed"] as? Bool == true else { throw APIError(status:403,code:"account_unavailable") }
        try Task.checkCancellation(); guard epoch == expected else { throw CancellationError() }
        let replacement = SavedSession(service:saved.service,project:saved.project,uid:saved.uid,refresh:try required(value,"refresh_token"))
        try storage.save(replacement)
        self.saved = replacement; token = idToken; expiry = Date().addingTimeInterval(seconds)
        accountID = saved.uid; email = account["email"] as? String ?? ""; allowance = AIAllowance(account["allowance"] as? [String:Any] ?? [:])
        allowed = true; message = ""; return idToken
    }
    func validToken() async throws -> String {
        guard allowed else { throw APIError(status:401,code:"sign_in_required") }
        if expiry.timeIntervalSinceNow > 120 { return token }
        if let renewal { return try await renewal.value }
        let generation = epoch
        let task = Task { try await self.renew(expected:generation) }; renewal = task
        defer { renewal = nil }
        do { return try await task.value } catch { handle(error, duringSession: true); throw error }
    }
    func cancel() {
        epoch = UUID(); operation?.cancel(); operation = nil; renewal?.cancel(); renewal = nil
        callback?.cancel(); callback = nil; busy = false
        if !allowed { message = "Sign in to use Callayers." }
    }
    func signOut() {
        cancel(); allowed = false; token = ""; saved = nil; accountID = ""; email = ""; expiry = .distantPast
        do { try storage.clear(); message = "Sign in to use Callayers." }
        catch { message = "Signed out here, but the remembered session couldn't be removed. Unlock your login Keychain and retry Sign out." }
    }
    func rejectSession() { allowed = false; token = ""; message = "Your sign-in expired. Sign in with Google again." }
    private func handle(_ error: Error, duringSession: Bool = false) {
        if error is CancellationError { if !allowed { message = "Sign-in canceled. Try again." }; return }
        let failure = error as? APIError
        let invalid = failure?.status == 401 || failure?.status == 403 || failure?.status == 400
        if invalid {
            allowed = false; token = ""; saved = nil; try? storage.clear()
        } else if !duringSession { allowed = false }
        message = (error as? APIError)?.errorDescription ?? "Couldn't finish sign-in. Check your connection and retry."
    }
}
