import Foundation
import Network
import Security
import CryptoKit

enum CallbackResult: Equatable { case ignore, cancelled, code(String) }
final class AuthCallback {
    private var listener: NWListener?
    private var continuation: CheckedContinuation<String, Error>?
    private var ready: CheckedContinuation<UInt16, Error>?
    private var timeout: Task<Void, Never>?
    private var connections: [ObjectIdentifier:NWConnection] = [:]
    private let queue = DispatchQueue(label: "com.callayers.oauth-callback")
    private let lock = NSLock()
    private var expectedState = ""
    private var finished = false
    private var result: Result<String, Error>?
    static func parse(target: String, expectedState: String) -> CallbackResult {
        guard target.utf8.count <= 8192, !expectedState.isEmpty, target.hasPrefix("/callback?"),
              let components = URLComponents(string: "http://127.0.0.1" + target), components.path == "/callback",
              let items = components.queryItems else { return .ignore }
        guard !items.contains(where: { ($0.value ?? "").unicodeScalars.contains { $0.value < 32 || $0.value == 127 } }) else { return .ignore }
        let states = items.filter { $0.name == "state" }
        guard states.count == 1, states[0].value == expectedState else { return .ignore }
        let codes = items.filter { $0.name == "code" }, errors = items.filter { $0.name == "error" }
        if errors.count == 1 && codes.isEmpty { return .cancelled }
        guard errors.isEmpty, codes.count == 1, let code = codes[0].value, !code.isEmpty, code.utf8.count <= 4096 else { return .ignore }
        return .code(code)
    }
    static func random(_ count: Int) throws -> String {
        var bytes = [UInt8](repeating: 0, count: count)
        guard SecRandomCopyBytes(kSecRandomDefault, count, &bytes) == errSecSuccess else { throw APIError(status: 0, code: "random_unavailable") }
        return Data(bytes).base64EncodedString().replacingOccurrences(of: "+", with: "-").replacingOccurrences(of: "/", with: "_").replacingOccurrences(of: "=", with: "")
    }
    static func challenge(_ verifier: String) -> String {
        Data(SHA256.hash(data: Data(verifier.utf8))).base64EncodedString().replacingOccurrences(of: "+", with: "-").replacingOccurrences(of: "/", with: "_").replacingOccurrences(of: "=", with: "")
    }
    func start(state: String) async throws -> UInt16 {
        expectedState = state
        let parameters = NWParameters.tcp
        parameters.requiredLocalEndpoint = .hostPort(host: "127.0.0.1", port: .any)
        let listener = try NWListener(using: parameters); self.listener = listener
        listener.newConnectionHandler = { [weak self] connection in self?.receive(connection) }
        return try await withCheckedThrowingContinuation { continuation in
            lock.lock(); if finished { lock.unlock(); continuation.resume(throwing:CancellationError()); return }; ready = continuation; lock.unlock()
            listener.stateUpdateHandler = { [weak self] value in
                guard let self else { return }
                if case .ready = value, let port = listener.port { self.resolveReady(.success(port.rawValue)) }
                if case .failed(let error) = value { self.resolveReady(.failure(error)); self.complete(.failure(error)) }
            }
            listener.start(queue: queue)
        }
    }
    private func resolveReady(_ result: Result<UInt16, Error>) {
        lock.lock(); let pending = ready; ready = nil; lock.unlock(); pending?.resume(with: result)
    }
    func wait() async throws -> String {
        try await withTaskCancellationHandler(operation: {
            try await withCheckedThrowingContinuation { value in
                lock.lock()
                if let result { lock.unlock(); value.resume(with: result); return }
                continuation = value; lock.unlock()
                timeout = Task { [weak self] in
                    do { try await Task.sleep(nanoseconds: 300_000_000_000); self?.cancel() } catch { }
                }
            }
        }, onCancel: { self.cancel() })
    }
    func cancel() { resolveReady(.failure(CancellationError())); complete(.failure(CancellationError())) }
    private func complete(_ result: Result<String, Error>) {
        lock.lock(); guard !finished else { lock.unlock(); return }; finished = true
        self.result = result
        let pending = continuation; continuation = nil; lock.unlock()
        pending?.resume(with: result); listener?.cancel(); timeout?.cancel()
    }
    private func receive(_ connection: NWConnection) {
        guard connections.count < 8 else { connection.cancel(); return }
        let id = ObjectIdentifier(connection); connections[id] = connection
        queue.asyncAfter(deadline:.now()+3) { [weak self,weak connection] in connection?.cancel(); self?.connections[id] = nil }
        connection.start(queue: queue)
        read(connection, data: Data())
    }
    private func read(_ connection: NWConnection, data: Data) {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 8192) { [weak self] bytes, _, done, error in
            guard let self else { connection.cancel(); return }
            let input = data + (bytes ?? Data())
            guard input.count <= 8192, error == nil else { connection.cancel(); return }
            guard let text = String(data: input, encoding: .utf8), text.contains("\r\n\r\n") else {
                if !done { self.read(connection, data: input) } else { connection.cancel() }; return
            }
            let line = text.components(separatedBy: "\r\n")[0].split(separator: " ")
            let parsed = line.count == 3 && line[0] == "GET" ? Self.parse(target: String(line[1]), expectedState: self.expectedState) : .ignore
            self.lock.lock(); let wasFinished = self.finished; self.lock.unlock()
            let accepted = parsed != .ignore && !wasFinished
            let message = accepted ? "Return to Callayers to finish signing in." : "This is not the expected sign-in callback. Return to Google to finish signing in."
            let body = "<!doctype html><meta charset=utf-8><title>Callayers</title><h1>Callayers</h1><p>\(message)</p>"
            let response = "HTTP/1.1 \(accepted ? "200 OK" : "400 Bad Request")\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: \(body.utf8.count)\r\n\r\n\(body)"
            connection.send(content: Data(response.utf8), completion: .contentProcessed { _ in connection.cancel() })
            if accepted {
                switch parsed { case .code(let code): self.complete(.success(code)); case .cancelled: self.complete(.failure(CancellationError())); case .ignore: break }
            }
        }
    }
}
