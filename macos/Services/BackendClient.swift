import Foundation

struct APIError: LocalizedError {
    let status: Int
    let code: String
    var errorDescription: String? {
        switch code {
        case "daily_limit_reached": return "Your daily AI allowance is used up. It resets at midnight UTC. Images, annotations and saved cards still work."
        case "website_tools_not_ready": return "Paste the page text instead of its web address."
        case "prompt_too_large", "invalid_attachments": return "Use at most eight text sources and 100,000 bytes of prompt and source text."
        case "sign_in_required", "invalid_session": return "Your sign-in expired. Sign in with Google again."
        case "account_unavailable", "google_sign_in_required": return "This account can't use Callayers. Try another Google account."
        case "request_already_used": return "That request was already submitted. A new generation uses another allowance unit."
        case "generation_failed": return "Couldn't create the card. Your existing work is safe. Try a new request."
        default: return "Couldn't connect to Callayers. Check your connection and retry."
        }
    }
}

final class BackendClient: NSObject, URLSessionTaskDelegate {
    let root: URL
    private var injected: URLSession?
    private lazy var session: URLSession = {
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = 90; config.timeoutIntervalForResource = 100
        config.httpCookieStorage = nil; config.urlCache = nil
        return URLSession(configuration: config, delegate: self, delegateQueue: nil)
    }()
    init(root: URL = BackendClient.serviceURL, session: URLSession? = nil) { self.root = root; self.injected = session }
    static var serviceURL: URL {
        let env = ProcessInfo.processInfo.environment
        let value = env["CALLAYERS_SERVICE_URL"] ?? env["SERVICE_URL"] ?? "https://callayers-v2-api-3fj7uh2a7a-uc.a.run.app"
        return URL(string: value.trimmingCharacters(in: .whitespacesAndNewlines)) ?? URL(string: "https://invalid.invalid")!
    }
    static func validService(_ url: URL) -> Bool {
        url.user == nil && url.password == nil && url.fragment == nil && url.query == nil && url.host != nil &&
        (url.scheme == "https" || (url.scheme == "http" && url.host == "127.0.0.1"))
    }
    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) { completionHandler(nil) }
    func request(_ path: String, method: String = "GET", body: [String: Any]? = nil, token: String? = nil) async throws -> [String: Any] {
        guard Self.validService(root) else { throw APIError(status: 0, code: "invalid_service") }
        return try await send(url: root.appendingPathComponent(path), method: method,
            data: body.map { try JSONSerialization.data(withJSONObject: $0) }, type: "application/json", token: token)
    }
    func send(url: URL, method: String, data: Data? = nil, type: String = "application/json", token: String? = nil) async throws -> [String: Any] {
        guard url.scheme == "https" || (url.scheme == "http" && url.host == "127.0.0.1"), url.user == nil, url.password == nil, url.fragment == nil else {
            throw APIError(status: 0, code: "invalid_service")
        }
        var request = URLRequest(url: url); request.httpMethod = method; request.httpBody = data
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        if data != nil { request.setValue(type, forHTTPHeaderField: "Content-Type") }
        if let token { request.setValue("Bearer " + token, forHTTPHeaderField: "Authorization") }
        let (bytes, response) = try await (injected ?? session).bytes(for: request)
        var result = Data()
        for try await byte in bytes {
            guard result.count < 2097152 else { throw APIError(status: 0, code: "response_too_large") }
            result.append(byte)
        }
        try Task.checkCancellation()
        guard let response = response as? HTTPURLResponse else { throw APIError(status: 0, code: "invalid_response") }
        let json = (try? JSONSerialization.jsonObject(with: result)) as? [String: Any] ?? [:]
        guard (200...299).contains(response.statusCode) else {
            throw APIError(status: response.statusCode, code: json["error"] as? String ?? "service_unavailable")
        }
        guard !json.isEmpty else { throw APIError(status: 0, code: "invalid_response") }
        return json
    }
    static func form(_ values: [String: String]) -> Data {
        let allowed = CharacterSet(charactersIn: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~")
        return Data(values.sorted { $0.key < $1.key }.map {
            ($0.key.addingPercentEncoding(withAllowedCharacters: allowed) ?? "") + "=" + ($0.value.addingPercentEncoding(withAllowedCharacters: allowed) ?? "")
        }.joined(separator: "&").utf8)
    }
}
