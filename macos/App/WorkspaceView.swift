import SwiftUI

@MainActor
struct WorkspaceView: View {
    @ObservedObject var model: WorkspaceModel
    @State private var search = ""
    @State private var renaming: UUID?
    @State private var name = ""
    @FocusState private var renameFocused: Bool
    @State private var deleting: WorkspaceCard?
    @State private var showDelete = false
    @State private var confirmRecovery = false

    var body: some View {
        HStack(spacing: 0) {
            VStack(alignment: .leading, spacing: 14) {
                Text("Callayers").font(.title2.weight(.semibold))
                Text("Mac workspace preview").font(.subheadline).foregroundColor(.secondary)
                HStack {
                    Button("Add counter") { model.addCounter() }
                        .disabled(!model.editable || model.document.cards.count >= 100)
                    Spacer()
                    Button("Hide all") { model.hideAll() }.disabled(!model.editable || model.visibleCards.isEmpty)
                }
                TextField("Search cards", text: $search)
                ScrollView {
                    LazyVStack(spacing: 4) {
                        ForEach(model.library.filter { search.isEmpty || $0.name.localizedCaseInsensitiveContains(search) }) { card in
                            row(card)
                        }
                        if model.document.cards.isEmpty {
                            Text("Add a counter to try saving and arranging cards.")
                                .font(.subheadline).foregroundColor(.secondary).padding(.top, 24)
                        }
                    }
                }
                if let card = model.selected {
                    Divider()
                    Text(card.name).font(.headline).lineLimit(1).help(card.name)
                    Text(card.visible ? "Drag the border to move; drag the corner to resize." : "Hidden from the preview.")
                        .font(.caption).foregroundColor(.secondary)
                    geometry("Horizontal", value: card.x, range: 0...(1280 - card.width)) { model.placeSelected(x: $0) }
                    geometry("Vertical", value: card.y, range: 0...(720 - card.height)) { model.placeSelected(y: $0) }
                    geometry("Size", value: card.scale, range: 0.5...1.4) { model.placeSelected(scale: $0) }
                    HStack {
                        Button("Duplicate") { model.duplicateSelected() }.disabled(!model.editable || model.document.cards.count >= 100)
                        Button("Bring to front") { model.bringToFront() }.disabled(!model.editable)
                    }
                }
                Divider()
                Toggle("Transparency grid", isOn: $model.checkerboard).toggleStyle(.checkbox)
                Text("Development preview. Camera and AI creation come next.")
                    .font(.caption).foregroundColor(.secondary)
            }.padding(20).frame(width: 285).background(Color(NSColor.windowBackgroundColor))
            Divider()
            VStack(alignment: .leading, spacing: 16) {
                HStack {
                    Text("Workspace").font(.headline)
                    Spacer()
                    Text(model.saving ? "Saving…" : model.loading ? "Opening cards…" : model.message)
                        .font(.caption).foregroundColor(.secondary)
                    Button("Save now") { model.save() }.disabled(!model.editable)
                }
                PreviewView(model: model).aspectRatio(16.0 / 9.0, contentMode: .fit)
                    .clipShape(RoundedRectangle(cornerRadius: 12))
                    .overlay(RoundedRectangle(cornerRadius: 12).stroke(Color.secondary.opacity(0.3)))
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                if !model.problem.isEmpty {
                    Text(model.problem).foregroundColor(.red).textSelection(.enabled)
                    HStack {
                        if model.locked {
                            Button("Retry opening workspace") { model.retryOpen() }
                            if model.canRecover { Button("Restore saved backup…") { confirmRecovery = true } }
                        } else if model.selected != nil {
                            Button("Reload saved card") { model.reloadSavedCard() }.disabled(model.saving)
                        }
                    }
                }
            }.padding(24).frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .alert("Delete card?", isPresented: $showDelete, presenting: deleting) { card in
            Button("Cancel", role: .cancel) { model.editingName = false }
            Button("Delete", role: .destructive) { model.editingName = false; model.delete(card.id) }
        } message: { card in Text("This removes \(card.name) from your library and workspace.") }
        .onChange(of: showDelete) { shown in if !shown { model.editingName = false } }
        .alert("Restore the saved backup?", isPresented: $confirmRecovery) {
            Button("Cancel", role: .cancel) {}
            Button("Restore backup") { model.recoverBackup() }
        } message: { Text("The previous saved workspace will reopen. The unreadable original will be preserved separately.") }
    }

    private func geometry(_ title: String, value: Double, range: ClosedRange<Double>, change: @escaping (Double) -> Void) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(title).font(.caption)
            Slider(value: Binding(get: { value }, set: change), in: range).disabled(!model.editable)
        }
    }
    private func startRename(_ card: WorkspaceCard) {
        guard model.editable else { return }
        model.editingName = true
        renaming = card.id; name = card.name; renameFocused = true
    }
    private func finishRename() {
        guard let id = renaming else { return }
        renaming = nil; model.editingName = false; model.rename(id, to: name)
    }
    private func row(_ card: WorkspaceCard) -> some View {
        HStack(spacing: 8) {
            Toggle("Show \(card.name)", isOn: Binding(get: { card.visible }, set: { model.setVisible(card.id, $0) }))
                .labelsHidden().toggleStyle(.checkbox).disabled(!model.editable)
            if renaming == card.id {
                TextField("Card name", text: $name)
                    .focused($renameFocused)
                    .onSubmit { finishRename() }
                    .onChange(of: renameFocused) { focused in if !focused { finishRename() } }
                    .onExitCommand { renaming = nil; model.editingName = false }
            } else {
                Text(card.name).lineLimit(1).help(card.name)
                    .frame(maxWidth: .infinity, alignment: .leading).contentShape(Rectangle())
                    .onTapGesture(count: 2) { startRename(card) }
                    .onTapGesture { model.select(card.id) }
            }
            Menu {
                Button("Rename") { startRename(card) }
                Button("Delete…") {
                    guard model.editable else { return }
                    model.editingName = true; deleting = card; showDelete = true
                }
            } label: { Image(systemName: "ellipsis") }.menuStyle(.borderlessButton).frame(width: 20)
                .disabled(!model.editable)
        }.padding(8)
            .background(model.document.selectedID == card.id ? Color.accentColor.opacity(0.18) : Color.clear)
            .cornerRadius(6)
    }
}
