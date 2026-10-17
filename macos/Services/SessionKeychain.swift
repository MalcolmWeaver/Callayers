import Foundation
import Security

struct SavedSession: Codable { let service: String; let project: String; let uid: String; let refresh: String }
protocol SessionStorage { func load() throws -> SavedSession?; func save(_ session: SavedSession) throws; func clear() throws }
struct SessionKeychain: SessionStorage {
    private var query: [String: Any] { [kSecClass as String: kSecClassGenericPassword,
        kSecAttrService as String: "com.callayers.mac.firebase", kSecAttrAccount as String: "session"] }
    func load() throws -> SavedSession? {
        var request = query; request[kSecReturnData as String] = true; request[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?; let status = SecItemCopyMatching(request as CFDictionary, &result)
        if status == errSecItemNotFound { return nil }
        guard status == errSecSuccess, let data = result as? Data else { throw APIError(status: 0, code: "keychain_locked") }
        return try JSONDecoder().decode(SavedSession.self, from: data)
    }
    func save(_ session: SavedSession) throws {
        let data = try JSONEncoder().encode(session)
        let result = SecItemUpdate(query as CFDictionary, [kSecValueData as String: data] as CFDictionary)
        if result == errSecItemNotFound {
            var request = query; request[kSecValueData as String] = data
            request[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
            guard SecItemAdd(request as CFDictionary, nil) == errSecSuccess else { throw APIError(status: 0, code: "keychain_locked") }
        } else if result != errSecSuccess { throw APIError(status: 0, code: "keychain_locked") }
    }
    func clear() throws {
        let result = SecItemDelete(query as CFDictionary)
        guard result == errSecSuccess || result == errSecItemNotFound else { throw APIError(status: 0, code: "keychain_locked") }
    }
}
