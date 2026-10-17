import SwiftUI
import AppKit

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    weak var model: WorkspaceModel?
    func applicationWillTerminate(_ notification: Notification) { model?.video.shutdown() }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard let model = model else { return .terminateNow }
        sender.keyWindow?.makeFirstResponder(nil)
        // Reply on the next run-loop turn even if an empty workspace saves
        // synchronously, after AppKit has received terminateLater.
        DispatchQueue.main.async {
            model.saveBeforeQuit { saved in
                if saved { sender.reply(toApplicationShouldTerminate: true); return }
                let alert = NSAlert()
                alert.messageText = "Your workspace could not be saved"
                alert.informativeText = "Cancel quitting to retry Save now. Quitting without saving may lose recent changes."
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
    @StateObject private var model = WorkspaceModel()
    @StateObject private var camera = CameraExtensionController()
    @State private var showCameraSetup = false
    @State private var handledLaunch = false

    init() {
        #if DEBUG
        let arguments = ProcessInfo.processInfo.arguments
        if let index = arguments.firstIndex(of: "--check-camera-transport") {
            // Exit before SwiftUI instantiates the workspace or opens a webcam.
            guard arguments.count == index + 4,
                  let duration = Double(arguments[index + 1]), duration.isFinite,
                  duration >= 5, duration <= 86400 else { exit(2) }
            // Independent of the main run loop, so a stalled camera API cannot
            // leave this isolated test producer alive indefinitely.
            DispatchQueue.global().asyncAfter(deadline: .now() + duration + 45) { _exit(124) }
            let reportURL = URL(fileURLWithPath: arguments[index + 2])
            var report = CLRunCameraTransportChecks(duration) as! [String: Any]
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
        WindowGroup("Callayers — Workspace Preview") {
            WorkspaceView(model: model)
                .frame(minWidth: 1000, idealWidth: 1280, minHeight: 640, idealHeight: 800)
                .onAppear {
                    delegate.model = model
                    // Explicit development launch option uses the same system approval flow as the button.
                    if !handledLaunch {
                        handledLaunch = true
                        if ProcessInfo.processInfo.arguments.contains("--activate-camera") {
                            showCameraSetup = true
                            camera.install()
                        }
                    }
                }
                .toolbar {
                    Button("Camera setup…") { showCameraSetup = true }
                }
                .sheet(isPresented: $showCameraSetup) { CameraSetupView(camera: camera) }
        }
        .commands {
            CommandGroup(replacing: .newItem) {}
            CommandGroup(after: .newItem) {
                Button("Save Workspace") { model.save() }.keyboardShortcut("s")
                    .disabled(!model.editable)
            }
        }
    }
}
