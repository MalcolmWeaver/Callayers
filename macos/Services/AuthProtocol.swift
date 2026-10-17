import Foundation
import CryptoKit
import Security
import LocalAuthentication

struct AuthConfig {
    let service: URL
    let project: String
    let client: String
    let secret: String
    let apiKey: String
    init(service: URL, object: [String: Any]) throws {
        guard (object["version"] as? NSNumber)?.intValue == 2 else { throw ServiceFailure.invalid }
        self.service = service
        project = try ServiceClient.string(object, "firebaseProjectId", max: 128)
        client = try ServiceClient.string(object, "googleDesktopClientId", max: 256)
        secret = try ServiceClient.string(object, "googleDesktopClientSecret", max: 256)
        apiKey = try ServiceClient.string(object, "firebaseApiKey", max: 256)
    }
}
struct SavedSession: Codable, Equatable {
    let service: URL
    let project: String
    let uid: String
    let refresh: String
}
struct AuthGrant {
    let saved: SavedSession
    let token: String
    let email: String
    let expires: Date
    var completeSignIn: ((Bool) -> Void)? = nil
}
enum AuthFailure: LocalizedError {
    case expired, denied, cancelled, storage, connection
    var errorDescription: String? {
        switch self {
        case .expired: return "Your sign-in expired. Sign in with Google again."
        case .denied: return "This account can't use Callayers. Try another Google account."
        case .cancelled: return "Sign-in cancelled."
        case .storage: return "Your sign-in could not be saved securely. Unlock your login Keychain and retry."
        case .connection: return "Couldn't finish sign-in. Check your connection and retry."
        }
    }
}
struct OAuthAttempt {
    let verifier: String
    let state: String
    init() throws {
        func random() throws -> String {
            var bytes = [UInt8](repeating: 0, count: 32)
            guard SecRandomCopyBytes(kSecRandomDefault, bytes.count, &bytes) == errSecSuccess else { throw AuthFailure.connection }
            return Self.base64(Data(bytes))
        }
        verifier = try random(); state = try random()
    }
    static func base64(_ data: Data) -> String { data.base64EncodedString().replacingOccurrences(of: "+", with: "-").replacingOccurrences(of: "/", with: "_").replacingOccurrences(of: "=", with: "") }
    static func challenge(_ verifier: String) -> String { base64(Data(SHA256.hash(data: Data(verifier.utf8)))) }
    func authorizationURL(config: AuthConfig, redirect: URL) -> URL {
        var url = URLComponents(string: "https://accounts.google.com/o/oauth2/v2/auth")!
        url.percentEncodedQuery = String(data: ServiceClient.form([
            "client_id": config.client, "redirect_uri": redirect.absoluteString, "response_type": "code",
            "scope": "openid email profile", "state": state, "code_challenge": Self.challenge(verifier),
            "code_challenge_method": "S256", "prompt": "select_account"
        ]), encoding: .utf8)
        return url.url!
    }
    static func callback(_ target: String, state: String) throws -> String {
        guard !state.isEmpty, target.utf8.count <= 8192, target.hasPrefix("/callback?"), !target.contains("#") else { throw ServiceFailure.invalid }
        var values: [String: [String]] = [:]
        for field in target.dropFirst(10).split(separator: "&", omittingEmptySubsequences: false) {
            let pair = field.split(separator: "=", maxSplits: 1, omittingEmptySubsequences: false)
            guard pair.count == 2 else { throw ServiceFailure.invalid }
            func decode(_ value: Substring) throws -> String {
                guard let result = String(value).replacingOccurrences(of: "+", with: " ").removingPercentEncoding,
                      !result.unicodeScalars.contains(where: { $0.value < 32 || $0.value == 127 }) else { throw ServiceFailure.invalid }
                return result
            }
            values[try decode(pair[0]), default: []].append(try decode(pair[1]))
        }
        guard values["state"] == [state] else { throw ServiceFailure.invalid }
        if let error = values["error"] {
            guard error.count == 1, values["code"] == nil else { throw ServiceFailure.invalid }
            throw AuthFailure.cancelled
        }
        guard let codes = values["code"], codes.count == 1, !codes[0].isEmpty, codes[0].utf8.count <= 4096 else { throw ServiceFailure.invalid }
        return codes[0]
    }
}
protocol SessionStore {
    func load() throws -> SavedSession?
    func save(_ session: SavedSession) throws
    func clear() throws
}
struct SessionKeychain: SessionStore {
    var account = "firebase"
    private var query: [String: Any] {
        let context = LAContext(); context.interactionNotAllowed = true
        return [kSecClass as String: kSecClassGenericPassword,
        kSecAttrService as String: "com.callayers.macos.session", kSecAttrAccount as String: account,
        kSecUseAuthenticationContext as String: context] }
    func load() throws -> SavedSession? {
        var request = query; request[kSecReturnData as String] = true; request[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?
        let status = SecItemCopyMatching(request as CFDictionary, &result)
        if status == errSecItemNotFound { return nil }
        guard status == errSecSuccess, let data = result as? Data, data.count <= 32_768 else { throw AuthFailure.storage }
        guard let saved = try? JSONDecoder().decode(SavedSession.self, from: data),
              saved.uid.utf8.count <= 128, !saved.uid.isEmpty, !saved.refresh.isEmpty, saved.refresh.utf8.count <= 16_384 else { throw AuthFailure.storage }
        return saved
    }
    func save(_ session: SavedSession) throws {
        let data = try JSONEncoder().encode(session)
        let attributes = [kSecValueData as String: data, kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly] as [String: Any]
        var status = SecItemUpdate(query as CFDictionary, attributes as CFDictionary)
        if status == errSecItemNotFound {
            var insert = query; attributes.forEach { insert[$0.key] = $0.value }
            status = SecItemAdd(insert as CFDictionary, nil)
        }
        guard status == errSecSuccess else { throw AuthFailure.storage }
    }
    func clear() throws {
        let status = SecItemDelete(query as CFDictionary)
        guard status == errSecSuccess || status == errSecItemNotFound else { throw AuthFailure.storage }
    }
}
