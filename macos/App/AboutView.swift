import SwiftUI

struct AboutView: View {
    @ObservedObject var updates: UpdateController
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        VStack(spacing: 18) {
            Image("CallayersMark").resizable().frame(width: 64, height: 64)
            Text("Callayers").font(.largeTitle.weight(.semibold))
            Text("Ideas in the picture").foregroundColor(.secondary)
            Text("Version \(Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "")")
            Text(updates.message).multilineTextAlignment(.center).foregroundColor(.secondary)
            if updates.ready {
                Button("Install update and restart") { updates.installWhenIdle() }.disabled(!updates.canInstall)
            } else { Button("Check for updates") { updates.check() }.disabled(!updates.canCheck) }
            Button("Done") { dismiss() }.keyboardShortcut(.defaultAction)
        }.padding(32).frame(width: 380)
    }
}
