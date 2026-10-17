import SwiftUI

@MainActor
final class ApplicationSession: ObservableObject {
    let auth = AuthController()
    @Published private(set) var workspace: WorkspaceModel?
    @Published private(set) var camera: VideoModel?
    @Published private(set) var ready = false
    private var telemetry: TelemetrySession?
    private var started = false
    private var timer: Timer?
    init() {
        auth.onCredentialsChanged = { [weak self] grant in self?.telemetry?.renew(grant) }
        auth.onAccessChanged = { [weak self] allowed in self?.accessChanged(allowed) }
    }
    deinit { timer?.invalidate() }
    func start() {
        guard !started else { return }; started = true; auth.restore()
        timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard let self, self.auth.allowed, !self.ready, let camera = self.camera else { return }
                camera.refreshDemand()
                guard camera.hasLiveCamera else { return }
                if self.workspace == nil { self.workspace = WorkspaceModel(video: camera, auth: self.auth) }
                self.workspace?.telemetry = self.telemetry
                self.ready = true
            }
        }
    }
    func finishTelemetry(_ completion: @escaping () -> Void) {
        camera?.shutdown()
        guard let telemetry else { completion(); return }
        telemetry.finish()
        Task {
            let deadline = Date().addingTimeInterval(2)
            while telemetry.bufferedCount > 0 && Date() < deadline { try? await Task.sleep(nanoseconds: 20_000_000) }
            completion()
        }
    }
    private func accessChanged(_ allowed: Bool) {
        ready = false
        if allowed {
            if let grant = auth.grant { telemetry = TelemetrySession(grant: grant) }
            if camera == nil { camera = VideoModel() }
            if let workspace { workspace.setAuthorized(true) } else { camera?.setAuthorized(true) }
            workspace?.telemetry = telemetry
            camera?.onTelemetry = { [weak self] state in self?.telemetry?.video(state) }
            NSApp.activate(ignoringOtherApps: true)
        } else {
            workspace?.setAuthorized(false); camera?.setAuthorized(false)
            telemetry?.finish(); telemetry = nil; workspace?.telemetry = nil; camera?.onTelemetry = nil
        }
    }
}

@MainActor
struct SessionView: View {
    @ObservedObject var session: ApplicationSession
    @ObservedObject var auth: AuthController
    var body: some View {
        Group {
            if auth.allowed, session.ready, let workspace = session.workspace {
                WorkspaceView(model: workspace)
            } else if auth.allowed, let camera = session.camera {
                VStack(spacing: 24) {
                    Text("Choose your webcam").font(.largeTitle.weight(.semibold))
                    Text("Your camera and cards will appear here.").foregroundColor(.secondary)
                    CameraPickerView(video: camera)
                    Button("Sign out") { auth.signOut() }
                }.padding(40).frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                VStack(spacing: 24) {
                    Text("Callayers").font(.system(size: 42, weight: .semibold))
                    Text("Ideas in the picture").font(.title3).foregroundColor(.secondary)
                    if auth.busy {
                        ProgressView()
                        Button("Cancel") { auth.cancel() }
                    } else {
                        Button("Sign in with Google") { auth.signIn() }.buttonStyle(.borderedProminent).controlSize(.large)
                        Button("Retry saved sign-in") { auth.restore() }
                    }
                    Text(auth.message).foregroundColor(.secondary).multilineTextAlignment(.center).frame(maxWidth: 440)
                }.padding(40).frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
    }
}
