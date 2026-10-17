import SwiftUI
import AppKit
import AVFoundation

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    weak var model: WorkspaceModel?
    weak var session: ApplicationSession?
    func applicationWillTerminate(_ notification: Notification) { model?.video.shutdown() }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard let model = model else {
            guard let session, session.auth.allowed else { return .terminateNow }
            DispatchQueue.main.async { session.finishTelemetry { sender.reply(toApplicationShouldTerminate: true) } }
            return .terminateLater
        }
        if model.video.hasConsumers {
            let alert = NSAlert()
            alert.messageText = "Quit while Callayers camera is in use?"
            alert.informativeText = "Your calling app will stop receiving your camera and cards."
            alert.addButton(withTitle: "Keep Callayers open"); alert.addButton(withTitle: "Quit")
            guard alert.runModal() == .alertSecondButtonReturn else { return .terminateCancel }
        }
        model.finishText()
        sender.keyWindow?.makeFirstResponder(nil)
        // Reply on the next run-loop turn even if an empty workspace saves
        // synchronously, after AppKit has received terminateLater.
        DispatchQueue.main.async {
            model.saveBeforeQuit { saved in
                if saved {
                    if let session = self.session { session.finishTelemetry { sender.reply(toApplicationShouldTerminate: true) } }
                    else { sender.reply(toApplicationShouldTerminate: true) }
                    return
                }
                let alert = NSAlert()
                alert.messageText = "Your workspace could not be saved"
                alert.informativeText = "Cancel quitting to retry saving. Quitting without saving may lose recent changes."
                alert.addButton(withTitle: "Cancel quit")
                alert.addButton(withTitle: "Quit without saving")
                sender.reply(toApplicationShouldTerminate: alert.runModal() == .alertSecondButtonReturn)
            }
        }
        return .terminateLater
    }
}

@main
@MainActor
struct CallayersRendererPreviewApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @StateObject private var session = ApplicationSession()
    @StateObject private var camera = CameraExtensionController()
    @State private var showCameraSetup = false
    @State private var handledLaunch = false

    init() {
        #if DEBUG
        let arguments = ProcessInfo.processInfo.arguments
        if let index = arguments.firstIndex(where: { $0 == "--check-camera-transport" || $0 == "--check-webcam-transport" }) {
            // Exit before SwiftUI instantiates the normal workspace and its camera pipeline.
            guard arguments.count == index + 5,
                  let owner = Int32(arguments[index + 4]), owner > 1,
                  let duration = Double(arguments[index + 1]), duration.isFinite,
                  duration >= 5, duration <= 86400 else { exit(2) }
            // Independent of the main run loop, so a stalled camera API cannot
            // leave this isolated test producer alive indefinitely.
            DispatchQueue.global().asyncAfter(deadline: .now() + duration + 45) { _exit(124) }
            let ownerWatch = DispatchSource.makeTimerSource(queue: .global())
            ownerWatch.schedule(deadline: .now(), repeating: 1)
            ownerWatch.setEventHandler { if kill(owner, 0) != 0 && errno == ESRCH { _exit(125) } }
            ownerWatch.resume()
            let reportURL = URL(fileURLWithPath: arguments[index + 2])
            var report: [String: Any]
            if arguments[index] == "--check-webcam-transport" {
                do {
                    guard AVCaptureDevice.authorizationStatus(for: .video) == .authorized else {
                        throw FrameComposer.error("Grant camera permission in Callayers before testing", -1)
                    }
                    let cameras = CameraCapture.devices()
                    let saved = UserDefaults.standard.string(forKey: "selectedWebcamID") ?? ""
                    guard let device = cameras.first(where: { $0.uniqueID == saved }) ?? (saved.isEmpty && cameras.count == 1 ? cameras.first : nil) else {
                        throw FrameComposer.error("Select a connected webcam in Callayers before testing", -1)
                    }
                    let producer = try WebcamTestProducer(cameraID: device.uniqueID)
                    report = CLRunCameraTransportChecks(duration, producer) as! [String: Any]
                    producer.stop()
                    report["webcam"] = device.localizedName
                } catch { report = ["passed": false, "error": error.localizedDescription] }
            } else { report = CLRunCameraTransportChecks(duration, nil) as! [String: Any] }
            ownerWatch.cancel()
            report["runID"] = arguments[index + 3]
            do {
                let data = try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
                try data.write(to: reportURL, options: .atomic)
            } catch { fputs("Cannot write camera test report: \(error)\n", stderr); exit(2) }
            exit((report["passed"] as? Bool) == true ? 0 : 1)
        }
        #endif
    }

    var body: some Scene {
        WindowGroup("Callayers") {
            SessionView(session: session, auth: session.auth)
                .frame(minWidth: 1000, idealWidth: 1280, minHeight: 640, idealHeight: 800)
                .onAppear {
                    delegate.session = session
                    session.start()
                    delegate.model = session.workspace
                    // Explicit development launch option uses the same system approval flow as the button.
                    if !handledLaunch {
                        handledLaunch = true
                        if ProcessInfo.processInfo.arguments.contains("--activate-camera") {
                            showCameraSetup = true
                            camera.install()
                        }
                    }
                }
                .onReceive(session.$workspace) { delegate.model = $0 }
                .preferredColorScheme(.dark)
                .toolbar {
                    Button("Camera setup…") { showCameraSetup = true }
                }
                .sheet(isPresented: $showCameraSetup) { CameraSetupView(camera: camera) }
        }
        .commands {
            CommandGroup(replacing: .newItem) {}
            CommandGroup(after: .newItem) {
                Button("Save Workspace") { session.workspace?.save() }.keyboardShortcut("s")
                    .disabled(session.workspace?.editable != true)
                Button("Sign out") { session.auth.signOut() }
            }
        }
    }
}
