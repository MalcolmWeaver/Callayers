import SwiftUI

@MainActor
struct WorkspaceView: View {
    @ObservedObject var model: PreviewModel

    var body: some View {
        HStack(spacing: 0) {
            VStack(alignment: .leading, spacing: 20) {
                VStack(alignment: .leading, spacing: 5) {
                    Text("Callayers").font(.title2.weight(.semibold))
                    Text("Mac renderer preview").font(.subheadline).foregroundColor(.secondary)
                }
                Divider()
                Text("LIBRARY").font(.caption.weight(.semibold)).foregroundColor(.secondary)
                Toggle("Interactive counter", isOn: Binding(
                    get: { model.shown }, set: { model.setVisible($0) }
                ))
                .toggleStyle(.checkbox)
                .disabled(model.busy || (!model.ready && model.shown))
                VStack(alignment: .leading, spacing: 8) {
                    Text("Card size").font(.subheadline)
                    Slider(value: $model.scale, in: 0.5...1.4)
                        .accessibilityLabel("Card size")
                        .disabled(!model.ready || model.busy)
                }
                Button("Save and reload card") { model.reload() }
                    .disabled(!model.ready || model.busy)
                Text("The counter and note should return after reloading. Saving also keeps them for the next launch.")
                    .font(.caption).foregroundColor(.secondary).fixedSize(horizontal: false, vertical: true)
                Spacer()
                Toggle("Show transparency grid", isOn: $model.checkerboard).toggleStyle(.checkbox)
                Text("This preview tests cards. Webcam capture and virtual-camera output come next.")
                    .font(.caption).foregroundColor(.secondary).fixedSize(horizontal: false, vertical: true)
            }
            .padding(24)
            .frame(width: 245)
            .frame(maxHeight: .infinity)
            .background(Color(NSColor.windowBackgroundColor))

            Divider()
            VStack(alignment: .leading, spacing: 16) {
                HStack {
                    Text("Preview").font(.headline)
                    Spacer()
                    Text("Private workspace").font(.caption).foregroundColor(.secondary)
                }
                PreviewView(model: model)
                    .aspectRatio(16.0 / 9.0, contentMode: .fit)
                    .clipShape(RoundedRectangle(cornerRadius: 12))
                    .overlay(RoundedRectangle(cornerRadius: 12).stroke(Color.secondary.opacity(0.3)))
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                if model.problem.isEmpty {
                    Text(model.message).font(.subheadline).foregroundColor(.secondary)
                } else {
                    HStack(alignment: .top) {
                        Text(model.problem).font(.subheadline).foregroundColor(.red).textSelection(.enabled)
                        Spacer()
                        Button("Reset preview") { model.resetPreview() }
                    }
                }
            }
            .padding(24)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
}
