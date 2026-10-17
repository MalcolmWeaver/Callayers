import Foundation
@main struct CallbackLiveChecks {
    static func main() async throws {
        let callback = AuthCallback()
        let port = try await callback.start(state:"expected-state")
        let wrongURL = URL(string:"http://127.0.0.1:\(port)/callback?state=wrong&code=not-accepted")!
        let (_,wrong) = try await URLSession.shared.data(from:wrongURL)
        guard (wrong as? HTTPURLResponse)?.statusCode == 400 else { throw NSError(domain:"Wrong-state callback accepted",code:1) }
        let goodURL = URL(string:"http://127.0.0.1:\(port)/callback?state=expected-state&code=accepted-code")!
        _ = try await URLSession.shared.data(from:goodURL)
        let code = try await callback.wait()
        guard code == "accepted-code" else { throw NSError(domain:"Callback before wait lost",code:1) }
        let canceled = AuthCallback(); _ = try await canceled.start(state:"cancel-state")
        canceled.cancel()
        do { _ = try await canceled.wait(); throw NSError(domain:"Canceled callback completed",code:1) } catch is CancellationError { }
        print("PASS live loopback callback: wrong state ignored, one valid callback retained before wait, cancellation")
    }
}
