import Foundation
import Combine

@MainActor
protocol AuthServicing {
    func config() async throws -> AuthConfig
    func refresh(_ saved: SavedSession, config: AuthConfig) async throws -> AuthGrant
    func signIn(config: AuthConfig) async throws -> AuthGrant
}

@MainActor
final class AuthService: AuthServicing {
    let http: HTTPTransport
    let service: URL
    init(http: HTTPTransport = ServiceClient(), service: URL = ServiceClient.production) { self.http = http; self.service = service }
    private func object(_ request: URLRequest) async throws -> [String: Any] {
        let response = try await http.send(request, limit: 65_536)
        if response.status == 401 || response.status == 400 { throw AuthFailure.expired }
        if response.status == 403 { throw AuthFailure.denied }
        guard (200...299).contains(response.status) else { throw AuthFailure.connection }
        return try response.object()
    }
    func config() async throws -> AuthConfig {
        try await AuthConfig(service: service, object: object(ServiceClient.request(service.appendingPathComponent("client-config"))))
    }
    func refresh(_ saved: SavedSession, config: AuthConfig) async throws -> AuthGrant {
        guard saved.service == config.service, saved.project == config.project else { throw AuthFailure.expired }
        let url = URL(string: "https://securetoken.googleapis.com/v1/token?key=" + ServiceClient.encode(config.apiKey))!
        let response = try await object(ServiceClient.request(url, method: "POST", body: ServiceClient.form([
            "grant_type": "refresh_token", "refresh_token": saved.refresh]), contentType: "application/x-www-form-urlencoded"))
        let token = try ServiceClient.string(response, "id_token")
        let refresh = try ServiceClient.string(response, "refresh_token")
        guard try ServiceClient.string(response, "user_id", max: 128) == saved.uid,
              let seconds = Double(try ServiceClient.string(response, "expires_in", max: 10)), (60...86_400).contains(seconds) else { throw AuthFailure.expired }
        let account = try await object(ServiceClient.request(service.appendingPathComponent("account"), token: token))
        guard try ServiceClient.string(account, "uid", max: 128) == saved.uid,
              let access = account["accessAllowed"] as? NSNumber, CFGetTypeID(access) == CFBooleanGetTypeID(), access.boolValue else { throw AuthFailure.denied }
        return AuthGrant(saved: SavedSession(service: saved.service, project: saved.project, uid: saved.uid, refresh: refresh),
                         token: token, email: (account["email"] as? String).map { String($0.prefix(256)) } ?? "", expires: Date().addingTimeInterval(seconds))
    }
    func signIn(config: AuthConfig) async throws -> AuthGrant {
        let attempt = try OAuthAttempt()
        let callback = LoopbackSignIn()
        let (code, redirect) = try await callback.authorize(config: config, attempt: attempt)
        var succeeded = false
        defer { callback.complete(success: succeeded) }
        try Task.checkCancellation()
        let google = try await object(ServiceClient.request(URL(string: "https://oauth2.googleapis.com/token")!, method: "POST",
            body: ServiceClient.form(["client_id": config.client, "client_secret": config.secret, "code": code,
                "redirect_uri": redirect.absoluteString, "grant_type": "authorization_code", "code_verifier": attempt.verifier]),
            contentType: "application/x-www-form-urlencoded"))
        let idToken = try ServiceClient.string(google, "id_token")
        let firebaseURL = URL(string: "https://identitytoolkit.googleapis.com/v1/accounts:signInWithIdp?key=" + ServiceClient.encode(config.apiKey))!
        let body: [String: Any] = ["requestUri": redirect.absoluteString, "returnSecureToken": true,
                                  "postBody": "id_token=" + ServiceClient.encode(idToken) + "&providerId=google.com"]
        let firebase = try await object(ServiceClient.request(firebaseURL, method: "POST", body: JSONSerialization.data(withJSONObject: body)))
        let saved = SavedSession(service: config.service, project: config.project, uid: try ServiceClient.string(firebase, "localId", max: 128),
                                 refresh: try ServiceClient.string(firebase, "refreshToken"))
        let result = try await refresh(saved, config: config)
        try Task.checkCancellation(); succeeded = true
        return result
    }
}

@MainActor
final class AuthController: ObservableObject {
    @Published private(set) var allowed = false
    @Published private(set) var busy = false
    @Published private(set) var message = "Sign in to use Callayers."
    @Published private(set) var email = ""
    private(set) var grant: AuthGrant?
    var onAccessChanged: ((Bool) -> Void)?
    private let service: AuthServicing
    private let store: SessionStore
    private var task: Task<Void, Never>?
    private var timer: Timer?
    private var epoch = UUID()
    private var nextRefresh = Date.distantFuture
    init(service: AuthServicing? = nil, store: SessionStore = SessionKeychain(), startTimer: Bool = true) {
        self.service = service ?? AuthService(); self.store = store
        if startTimer {
            timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
                Task { @MainActor in self?.checkExpiry() }
            }
        }
    }
    deinit { timer?.invalidate(); task?.cancel() }
    var token: String? { allowed && (grant?.expires ?? .distantPast) > Date() ? grant?.token : nil }
    func restore() { run(interactive: false) }
    func signIn() { run(interactive: true) }
    func cancel() {
        epoch = UUID(); task?.cancel(); task = nil; busy = false
        if !allowed { message = "Sign-in cancelled. You can try again." }
    }
    func signOut() {
        cancel(); setGrant(nil)
        do { try store.clear(); message = "Signed out. Your cards remain on this Mac." }
        catch { message = "Signed out here, but the saved session could not be removed. Unlock your login Keychain and sign out again." }
    }
    func rejectSession() {
        cancel(); setGrant(nil); try? store.clear(); message = AuthFailure.expired.localizedDescription
    }
    func checkExpiry(now: Date = Date()) {
        guard let grant else { return }
        if now >= grant.expires { setGrant(nil); message = AuthFailure.expired.localizedDescription; return }
        if now >= nextRefresh && !busy { run(interactive: false) }
    }
    private func setGrant(_ value: AuthGrant?) {
        let wasAllowed = allowed
        grant = value; email = value?.email ?? ""; allowed = value != nil
        nextRefresh = value?.expires.addingTimeInterval(-120) ?? .distantFuture
        if wasAllowed != allowed { onAccessChanged?(allowed) }
    }
    private func run(interactive: Bool) {
        guard !busy else { return }
        let ticket = UUID(); epoch = ticket; busy = true; message = interactive ? "Finish signing in in your browser…" : "Checking your sign-in…"
        task = Task { [weak self] in
            guard let self else { return }
            do {
                let saved: SavedSession? = try interactive ? nil : (grant?.saved ?? store.load())
                if !interactive && saved == nil {
                    guard epoch == ticket else { return }; busy = false; message = "Sign in to use Callayers."; return
                }
                let config = try await service.config()
                guard epoch == ticket else { return }
                let result: AuthGrant
                if let saved { result = try await service.refresh(saved, config: config) }
                else { result = try await service.signIn(config: config) }
                guard epoch == ticket, !Task.isCancelled else { return }
                try store.save(result.saved)
                setGrant(result); message = ""; busy = false
            } catch {
                guard epoch == ticket else { return }
                busy = false
                let invalid = error as? AuthFailure
                if invalid == .expired || invalid == .denied { setGrant(nil); try? store.clear() }
                else if grant?.expires ?? .distantPast <= Date() { setGrant(nil) }
                nextRefresh = Date().addingTimeInterval(30)
                message = (error as? LocalizedError)?.errorDescription ?? AuthFailure.connection.localizedDescription
            }
        }
    }
}
