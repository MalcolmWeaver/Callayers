import SwiftUI
import AppKit

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

@main
struct CallayersRendererPreviewApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @StateObject private var model = PreviewModel()

    var body: some Scene {
        WindowGroup("Callayers — Renderer Preview") {
            WorkspaceView(model: model)
                .frame(minWidth: 900, idealWidth: 1180, minHeight: 580, idealHeight: 740)
        }
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
    }
}
