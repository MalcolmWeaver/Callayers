import Foundation
import Network
import AppKit

@MainActor
final class LoopbackSignIn {
    private var browserConnection: NWConnection?
    private var listener: NWListener?
    private var connections: [UUID: NWConnection] = [:]
    private var continuation: CheckedContinuation<(String, URL), Error>?
    private var timeout: Task<Void, Never>?
    private var redirect: URL?
    private var state = ""

    func authorize(config: AuthConfig, attempt: OAuthAttempt,
                   open: @escaping @MainActor (URL) -> Bool = { NSWorkspace.shared.open($0) }) async throws -> (String, URL) {
        try Task.checkCancellation()
        return try await withTaskCancellationHandler(operation: {
            try await withCheckedThrowingContinuation { continuation in
                self.continuation = continuation; state = attempt.state
                do {
                    let parameters = NWParameters.tcp
                    parameters.requiredLocalEndpoint = .hostPort(host: "127.0.0.1", port: .any)
                    let listener = try NWListener(using: parameters)
                    self.listener = listener
                    listener.stateUpdateHandler = { [weak self] status in
                        Task { @MainActor in
                            guard let self, self.continuation != nil else { return }
                            switch status {
                            case .ready:
                                guard self.redirect == nil, let port = listener.port,
                                      let url = URL(string: "http://127.0.0.1:\(port.rawValue)/callback") else { return }
                                self.redirect = url
                                if !open(attempt.authorizationURL(config: config, redirect: url)) { self.finish(.failure(AuthFailure.connection)) }
                            case .failed: self.finish(.failure(AuthFailure.connection))
                            default: break
                            }
                        }
                    }
                    listener.newConnectionHandler = { [weak self] connection in
                        Task { @MainActor in self?.accept(connection) }
                    }
                    listener.start(queue: .main)
                    timeout = Task { [weak self] in
                        do { try await Task.sleep(nanoseconds: 180_000_000_000) } catch { return }
                        self?.finish(.failure(AuthFailure.connection))
                    }
                } catch { finish(.failure(AuthFailure.connection)) }
            }
        }, onCancel: { Task { @MainActor in self.cancel() } })
    }
    func cancel() { finish(.failure(AuthFailure.cancelled)); complete(success: false) }
    func complete(success: Bool) {
        guard let connection = browserConnection else { return }
        browserConnection = nil
        send(connection, status: "200 OK", message: success ? "Signed in. You can return to Callayers and close this tab." : "Sign-in could not be completed. Return to Callayers to retry.")
    }
    private func accept(_ connection: NWConnection) {
        guard continuation != nil, connections.count < 8 else { connection.cancel(); return }
        let id = UUID(); connections[id] = connection
        connection.start(queue: .main); receive(id, Data())
        Task { [weak self, weak connection] in
            try? await Task.sleep(nanoseconds: 5_000_000_000)
            if self?.connections.removeValue(forKey: id) != nil { connection?.cancel() }
        }
    }
    private func receive(_ id: UUID, _ existing: Data) {
        guard let connection = connections[id] else { return }
        connection.receive(minimumIncompleteLength: 1, maximumLength: 8192 - existing.count) { [weak self] data, _, complete, error in
            Task { @MainActor in
                guard let self, self.connections[id] != nil else { return }
                var bytes = existing; if let data { bytes.append(data) }
                if bytes.range(of: Data("\r\n\r\n".utf8)) != nil { self.process(id, bytes); return }
                if bytes.count >= 8192 || complete || error != nil { self.respond(id, status: "400 Bad Request", message: "Invalid sign-in callback."); return }
                self.receive(id, bytes)
            }
        }
    }
    private func process(_ id: UUID, _ bytes: Data) {
        guard continuation != nil, let redirect, let request = String(data: bytes, encoding: .utf8) else {
            respond(id, status: "400 Bad Request", message: "Invalid sign-in callback."); return
        }
        let lines = request.components(separatedBy: "\r\n")
        let first = lines[0].split(separator: " ")
        let hosts = lines.dropFirst().filter { $0.lowercased().hasPrefix("host:") }.map { $0.dropFirst(5).trimmingCharacters(in: .whitespaces) }
        guard first.count == 3, first[0] == "GET", first[2] == "HTTP/1.1",
              hosts == ["127.0.0.1:\(redirect.port!)"] else {
            respond(id, status: "400 Bad Request", message: "Invalid sign-in callback."); return
        }
        do {
            let code = try OAuthAttempt.callback(String(first[1]), state: state)
            browserConnection = connections.removeValue(forKey: id)
            finish(.success((code, redirect)))
        } catch AuthFailure.cancelled {
            respond(id, status: "200 OK", message: "Sign-in cancelled. You can close this tab.")
            finish(.failure(AuthFailure.cancelled))
        } catch { respond(id, status: "400 Bad Request", message: "Invalid sign-in callback. Please return to Callayers.") }
    }
    private func respond(_ id: UUID, status: String, message: String) {
        guard let connection = connections.removeValue(forKey: id) else { return }
        send(connection, status: status, message: message)
    }
    private func send(_ connection: NWConnection, status: String, message: String) {
        let body = Data(message.utf8)
        var response = Data("HTTP/1.1 \(status)\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: \(body.count)\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nConnection: close\r\n\r\n".utf8)
        response.append(body)
        connection.send(content: response, completion: .contentProcessed { _ in connection.cancel() })
    }
    private func finish(_ result: Result<(String, URL), Error>) {
        let pending = continuation; continuation = nil
        listener?.cancel(); listener = nil
        connections.values.forEach { $0.cancel() }; connections.removeAll()
        timeout?.cancel(); timeout = nil; state = ""; redirect = nil
        pending?.resume(with: result)
    }
}
