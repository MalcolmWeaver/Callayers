import Foundation

struct ServiceFailure: LocalizedError {
    let message: String
    var errorDescription: String? { message }
    static let invalid = ServiceFailure(message: "Callayers received an invalid response. Please try again.")
}
struct HTTPResult {
    let status: Int
    let body: Data
    func object() throws -> [String: Any] {
        guard let value = try JSONSerialization.jsonObject(with: body) as? [String: Any] else { throw ServiceFailure.invalid }
        return value
    }
}
protocol HTTPTransport {
    func send(_ request: URLRequest, limit: Int) async throws -> HTTPResult
}
final class ServiceClient: NSObject, HTTPTransport, URLSessionTaskDelegate {
    private lazy var session: URLSession = {
        let config = URLSessionConfiguration.ephemeral
        config.httpCookieStorage = nil; config.urlCredentialStorage = nil; config.urlCache = nil
        config.timeoutIntervalForRequest = 15; config.timeoutIntervalForResource = 125
        return URLSession(configuration: config, delegate: self, delegateQueue: nil)
    }()
    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) { completionHandler(nil) }
    func send(_ request: URLRequest, limit: Int = 1_048_576) async throws -> HTTPResult {
        let (bytes, response) = try await session.bytes(for: request)
        guard let response = response as? HTTPURLResponse, response.expectedContentLength <= limit else { throw ServiceFailure.invalid }
        var data = Data()
        for try await byte in bytes {
            guard data.count < limit else { throw ServiceFailure.invalid }
            data.append(byte)
        }
        return HTTPResult(status: response.statusCode, body: data)
    }
    static let production = URL(string: "https://callayers-v2-api-3fj7uh2a7a-uc.a.run.app")!
    static func serviceURL(_ text: String) throws -> URL {
        guard let parts = URLComponents(string: text), parts.scheme == "https", parts.host != nil,
              parts.user == nil, parts.password == nil, parts.query == nil, parts.fragment == nil,
              parts.path.isEmpty || parts.path == "/", let url = parts.url else { throw ServiceFailure.invalid }
        return url
    }
    static func request(_ url: URL, method: String = "GET", body: Data? = nil, token: String? = nil,
                        contentType: String = "application/json", timeout: TimeInterval = 15) -> URLRequest {
        var request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: timeout)
        request.httpMethod = method; request.httpBody = body
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        if body != nil { request.setValue(contentType, forHTTPHeaderField: "Content-Type") }
        if let token { request.setValue("Bearer " + token, forHTTPHeaderField: "Authorization") }
        return request
    }
    static func encode(_ value: String) -> String {
        value.addingPercentEncoding(withAllowedCharacters: CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~"))!
    }
    static func form(_ values: [String: String]) -> Data {
        Data(values.keys.sorted().map { encode($0) + "=" + encode(values[$0]!) }.joined(separator: "&").utf8)
    }
    static func string(_ object: [String: Any], _ key: String, max: Int = 16_384) throws -> String {
        guard let value = object[key] as? String, !value.isEmpty, value.utf8.count <= max,
              !value.unicodeScalars.contains(where: { $0.value < 32 || $0.value == 127 }) else { throw ServiceFailure.invalid }
        return value
    }
}
