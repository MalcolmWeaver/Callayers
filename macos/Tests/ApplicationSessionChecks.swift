#if DEBUG
import AppKit
import AVFoundation

private final class LifecycleStore: SessionStore {
    var value: SavedSession?
    func load() throws -> SavedSession? { value }
    func save(_ value: SavedSession) throws { self.value = value }
    func clear() throws { value = nil }
}
@MainActor
private final class LifecycleAuth: AuthServicing {
    func config() async throws -> AuthConfig {
        try AuthConfig(service: ServiceClient.production, object: ["version": 2, "firebaseProjectId": "fixture", "googleDesktopClientId": "fixture", "googleDesktopClientSecret": "fixture", "firebaseApiKey": "fixture"])
    }
    func refresh(_ saved: SavedSession, config: AuthConfig) async throws -> AuthGrant { try await signIn(config: config) }
    func signIn(config: AuthConfig) async throws -> AuthGrant {
        AuthGrant(saved: SavedSession(service: config.service, project: config.project, uid: "fixture", refresh: "fixture"), token: "fixture", email: "fixture@example.invalid", expires: Date().addingTimeInterval(3600))
    }
}
@MainActor
enum ApplicationSessionChecks {
    private static func require(_ condition: @autoclosure () -> Bool, _ message: String) throws {
        if !condition() { throw ServiceFailure(message: message) }
    }
    private static func until(_ message: String, _ condition: () -> Bool) async throws {
        let deadline = Date().addingTimeInterval(20)
        while !condition() {
            if Date() > deadline { throw ServiceFailure(message: message) }
            try await Task.sleep(nanoseconds: 50_000_000)
        }
    }
    static func begin(reportURL: URL, runID: String) {
        Task { @MainActor in
            var checks: [[String: Any]] = []
            var report: [String: Any] = ["runID": runID, "passed": false]
            let directory = FileManager.default.temporaryDirectory.appendingPathComponent("Callayers-lifecycle-" + UUID().uuidString)
            var session: ApplicationSession?
            defer { session?.camera?.shutdown(); try? FileManager.default.removeItem(at: directory) }
            do {
                guard AVCaptureDevice.authorizationStatus(for: .video) == .authorized else { throw ServiceFailure(message: "Camera permission is required") }
                let store = WorkspaceStore(directory: directory), credentials = LifecycleStore()
                let auth = AuthController(service: LifecycleAuth(), store: credentials, startTimer: false)
                var visible = true
                let app = ApplicationSession(auth: auth, createCamera: { VideoModel(previewDemand: { visible }) },
                    createWorkspace: { WorkspaceModel(video: $0, auth: $1, workspaceStore: store) },
                    createTelemetry: { _ in nil }, activate: {})
                session = app; app.start()
                try await until("Saved-session check did not settle") { !auth.busy }
                try require(app.camera == nil && app.workspace == nil && !app.ready, "Camera or workspace started before account access")
                checks.append(["name": "account gate prevents camera and workspace startup", "passed": true])
                auth.signIn()
                try await until("Verified account and live webcam did not open workspace") { app.ready && app.workspace?.editable == true }
                guard let model = app.workspace, let camera = app.camera else { throw ServiceFailure.invalid }
                try require(camera.hasLiveCamera && !camera.hasConsumers, "Expected fresh webcam with no calling app attached")
                model.addCounter()
                try await until("Real card renderer did not become ready") { model.editable && model.document.cards.count == 1 && model.renderers.values.first?.ready == true && camera.preview != nil }
                guard let card = model.selected, let renderer = model.renderers[card.id] else { throw ServiceFailure.invalid }
                renderer.mouse(0, x: 75, y: 225, button: 0)
                renderer.mouse(1, x: 75, y: 225, button: 1)
                renderer.mouse(2, x: 75, y: 225, button: 1)
                try await Task.sleep(nanoseconds: 150_000_000)
                model.save()
                try await until("Card state was not saved") { !model.saving }
                let saved = try store.load()
                let state = try JSONSerialization.jsonObject(with: Data(saved.cards[0].state.utf8)) as? [String: Any]
                try require(state?["count"] as? Int == 1, "Actual renderer input did not persist counter value")
                checks.append(["name": "verified session opens real webcam, renderer and saved interactive state", "passed": true])
                let imageURL = directory.appendingPathComponent("fixture-image.png")
                guard let pixels = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 2, pixelsHigh: 2,
                    bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                    colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0), let bytes = pixels.bitmapData else { throw ServiceFailure.invalid }
                memset(bytes, 255, pixels.bytesPerRow * pixels.pixelsHigh)
                guard let png = pixels.representation(using: .png, properties: [:]) else { throw ServiceFailure.invalid }
                try png.write(to: imageURL)
                model.importImage(from: imageURL)
                try await until("Image import did not save") { !model.importing && !model.saving && model.document.cards.count == 2 }
                guard let imported = model.document.cards.first(where: { $0.imageID != nil }) else { throw ServiceFailure.invalid }
                try require(!imported.visible && model.renderers[imported.id] == nil, "An imported image appeared before its checkbox was selected")
                model.setVisible(imported.id, true)
                try await until("Showing the imported image did not create its renderer") { model.editable && model.renderers[imported.id]?.ready == true }
                model.setVisible(imported.id, false)
                try await until("Hiding the imported image did not unload it") { !model.saving && model.renderers[imported.id] == nil }
                model.select(card.id)
                checks.append(["name": "real image import saves hidden and checkbox visibility controls rendering", "passed": true])
                visible = false
                try await until("Idle session did not release renderers and capture") { !camera.wantsFrames && !camera.hasLiveCamera && model.renderers.isEmpty && !model.saving }
                visible = true
                try await until("Preview demand did not restore camera and renderers") { camera.hasLiveCamera && model.renderers[card.id]?.ready == true && model.editable }
                model.save()
                try await until("Resumed renderer state did not save") { !model.saving }
                let restored = try JSONSerialization.jsonObject(with: Data(store.load().cards[0].state.utf8)) as? [String: Any]
                try require(restored?["count"] as? Int == 1, "Idle resume lost card state")
                checks.append(["name": "idle pause releases resources and resume restores interactive values", "passed": true])
                auth.signOut()
                try await until("Sign-out did not stop capture and save") { !camera.hasLiveCamera && !model.saving && model.renderers.isEmpty }
                try require(!app.ready && !model.editable && !camera.wantsFrames && camera.preview == nil && credentials.value == nil, "Sign-out left active access or credentials")
                let signedOut = try store.load()
                try require(signedOut.cards.count == 2, "Sign-out lost local cards")
                auth.signIn()
                try await until("Second sign-in did not restore workspace") { app.ready && model.editable && model.renderers[card.id]?.ready == true }
                try require(app.workspace === model && model.document.cards[0].id == card.id, "Second sign-in replaced local workspace")
                checks.append(["name": "sign-out stops video and later verified sign-in retains local cards", "passed": true])
                auth.signOut()
                try await until("Final session save did not finish") { !model.saving && !camera.hasLiveCamera }
                report["passed"] = true
            } catch { report["error"] = error.localizedDescription }
            session?.camera?.shutdown(); session = nil
            try? await Task.sleep(nanoseconds: 100_000_000)
            report["checks"] = checks
            do { try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys]).write(to: reportURL, options: .atomic) }
            catch { fputs("Could not save lifecycle report\n", stderr); exit(2) }
            try? FileManager.default.removeItem(at: directory)
            exit(report["passed"] as? Bool == true ? 0 : 1)
        }
    }
}
#endif
