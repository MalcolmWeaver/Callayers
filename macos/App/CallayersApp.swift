import SwiftUI
import AppKit

@MainActor final class AppDelegate: NSObject, NSApplicationDelegate {
    weak var model: WorkspaceModel?
    func applicationWillTerminate(_ notification: Notification) { model?.video.shutdown() }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard let model else { return .terminateNow }
        if model.video.consumerCount > 0 {
            let alert = NSAlert(); alert.messageText = "Quit while the Callayers camera is in use?"
            alert.informativeText = "Video in your calling app will stop. Your cards will be saved."
            alert.addButton(withTitle:"Keep Callayers open"); alert.addButton(withTitle:"Quit")
            if alert.runModal() == .alertFirstButtonReturn { return .terminateCancel }
        }
        sender.keyWindow?.makeFirstResponder(nil)
        DispatchQueue.main.async {
            model.saveBeforeQuit { saved in
                if saved { model.generation.cancel(); sender.reply(toApplicationShouldTerminate:true); return }
                let alert = NSAlert(); alert.messageText = "Your workspace could not be saved"
                alert.informativeText = "Cancel quitting to retry saving. Quitting without saving may lose recent changes."
                alert.addButton(withTitle:"Cancel quit"); alert.addButton(withTitle:"Quit without saving")
                sender.reply(toApplicationShouldTerminate:alert.runModal() == .alertSecondButtonReturn)
            }
        }
        return .terminateLater
    }
}
struct AppRootView: View {
    @ObservedObject var model: WorkspaceModel
    @ObservedObject var auth: AuthController
    @ObservedObject var video: VideoModel
    @ObservedObject var camera: CameraExtensionController
    @State private var showCameraSetup = false
    var body: some View {
        Group {
            if !auth.allowed { SignInView(auth:auth) }
            else if !video.cameraReady { CameraRecoveryView(video:video) { auth.signOut() } }
            else { WorkspaceView(model:model,video:video,annotations:model.annotations,showCameraSetup:$showCameraSetup) }
        }.sheet(isPresented:$showCameraSetup) { CameraSetupView(camera:camera) }
        .frame(minWidth:1100,minHeight:720).preferredColorScheme(.dark)
        .onAppear { if ProcessInfo.processInfo.arguments.contains("--activate-camera") { showCameraSetup = true; camera.install() } }
    }
}
@main @MainActor struct CallayersApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @StateObject private var model = WorkspaceModel()
    @StateObject private var camera = CameraExtensionController()
    var body: some Scene {
        WindowGroup("Callayers") {
            AppRootView(model:model,auth:model.auth,video:model.video,camera:camera)
                .onAppear { delegate.model = model }
        }.commands {
            CommandGroup(replacing:.newItem) { }
            CommandGroup(after:.appInfo) { Button("Camera setup…") { camera.install() }.disabled(!camera.canInstall) }
        }
    }
}
