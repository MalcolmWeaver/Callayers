import SwiftUI

struct CameraSetupView: View {
    @ObservedObject var camera: CameraExtensionController
    @Environment(\.dismiss) private var dismiss
    @State private var confirmRemoval = false

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            Text("Callayers camera").font(.title2.weight(.semibold))
            Text("Choose a webcam in the workspace, then select Callayers in your calling app. Install again here to update an older camera extension.")
                .foregroundColor(.secondary)
            Text(camera.prerequisite ?? camera.message).textSelection(.enabled)
            if camera.needsApproval {
                Button("Open System Settings") { camera.openSettings() }
            }
            HStack {
                Button("Install camera") { camera.install() }.disabled(!camera.canInstall)
                Button("Remove camera…") { confirmRemoval = true }.disabled(!camera.canInstall)
                if camera.busy { ProgressView().controlSize(.small) }
                Spacer()
                Button("Done") { dismiss() }.keyboardShortcut(.cancelAction)
            }
        }.padding(24).frame(width: 540)
            .alert("Remove the Callayers camera?", isPresented: $confirmRemoval) {
                Button("Cancel", role: .cancel) {}
                Button("Remove", role: .destructive) { camera.remove() }
            } message: { Text("Calling apps will no longer be able to select Callayers. You can install it again here.") }
    }
}
