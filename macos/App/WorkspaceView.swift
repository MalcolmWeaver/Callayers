import SwiftUI

@MainActor
struct WorkspaceView: View {
    @ObservedObject var model: WorkspaceModel
    @State private var search = ""
    @State private var renaming: WorkspaceCard?
    @State private var name = ""
    @FocusState private var renameFocused: Bool
    @State private var deleting: WorkspaceCard?
    @State private var showDelete = false
    @State private var confirmRecovery = false
    @State private var confirmClear = false

    var body: some View {
        HStack(spacing: 0) {
            VStack(alignment: .leading, spacing: 10) {
                HStack(spacing: 10) {
                    Image("CallayersMark").resizable().frame(width: 32, height: 32)
                    Text("Callayers").font(.title2.weight(.semibold))
                }
                HStack {
                    Button("Create with AI") { model.select(nil) }
                        .disabled(!model.editable || model.document.cards.count >= 100)
                    Button(model.importing ? "Importing…" : "Import image") { model.importImage() }
                        .disabled(!model.editable || model.document.cards.count >= 100)
                }
                HStack {
                    TextField("Search cards", text: $search)
                    Button("Hide all") { model.hideAll() }.disabled(!model.editable || model.visibleCards.isEmpty)
                }
                ScrollView {
                    LazyVStack(spacing: 4) {
                        ForEach(model.library.filter { search.isEmpty || $0.name.localizedCaseInsensitiveContains(search) }) { card in
                            row(card)
                        }
                        if model.document.cards.isEmpty {
                            Text("Create your first card")
                                .font(.subheadline).foregroundColor(.secondary).padding(.top, 24)
                        }
                    }
                }
                if let card = model.selected {
                    Divider()
                    DisclosureGroup {
                    Text(card.visible ? "Drag the border to move; drag the corner to resize." : "Hidden from the preview.")
                        .font(.caption).foregroundColor(.secondary)
                    geometry("Horizontal", value: card.x, range: 0...(1280 - card.width)) { model.placeSelected(x: $0) }
                    geometry("Vertical", value: card.y, range: 0...(720 - card.height)) { model.placeSelected(y: $0) }
                    geometry("Size", value: card.scale, range: 0.1...card.maximumScale) { model.placeSelected(scale: $0) }
                    HStack {
                        Button("Duplicate") { model.duplicateSelected() }.disabled(!model.editable || model.document.cards.count >= 100)
                        Button("Bring to front") { model.bringToFront() }.disabled(!model.editable)
                    }
                    } label: { Text("Position and size").font(.subheadline) }
                }
                Divider()
                VStack(alignment: .leading, spacing: 8) {
                    Text(model.selected.map { "Edit “\($0.name)”" } ?? "New card").font(.headline).lineLimit(1)
                    Text(model.selected == nil ? "Describe what you want to show." : "Describe what you want to change.")
                        .font(.caption).foregroundColor(.secondary)
                    TextEditor(text: Binding(get: { model.prompt }, set: { model.setPrompt($0) }))
                        .font(.body).frame(height: 90).overlay(RoundedRectangle(cornerRadius: 4).stroke(Color.secondary.opacity(0.3)))
                        .accessibilityLabel("Card prompt")
                    HStack {
                        Button("Send") { model.generate() }.disabled(!model.editable || model.generating || model.prompt.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                        if model.generating { ProgressView().controlSize(.small); Button("Cancel") { model.cancelGeneration() } }
                    }
                    if !model.generationMessage.isEmpty { Text(model.generationMessage).font(.caption).foregroundColor(.secondary).lineLimit(4).help(model.generationMessage) }
                }
                Divider()
                Text("Choose Callayers as the camera in your calling app.")
                    .font(.caption).foregroundColor(.secondary)
            }.padding(16).frame(width: 285).background(Color(NSColor.windowBackgroundColor))
            Divider()
            VStack(alignment: .leading, spacing: 16) {
                HStack {
                    CameraPickerView(video: model.video)
                    Button("Annotate") {
                        model.finishText(); model.annotationToolbar.toggle()
                        if !model.annotationToolbar { model.annotationDraft = nil; model.selectedAnnotation = nil }
                    }
                }
                if model.annotationToolbar {
                    HStack(spacing: 8) {
                        Picker("Tool", selection: $model.annotationTool) {
                            ForEach(AnnotationTool.allCases, id: \.self) { tool in Text(tool.rawValue.capitalized).tag(tool) }
                        }.frame(width: 150)
                        ForEach([UInt32(0x56ddca), 0xffffff, 0x111827, 0xff5555, 0xffcf55, 0x5599ff], id: \.self) { color in
                            Button { model.setAnnotationColor(color) } label: {
                                Circle().fill(Color(AnnotationRenderer.color(color))).frame(width: 20, height: 20)
                                    .overlay(Circle().stroke(model.annotationColor == color ? Color.accentColor : Color.clear, lineWidth: 3))
                            }.buttonStyle(.plain).accessibilityLabel("Annotation color \(String(color, radix: 16))")
                        }
                        Slider(value: $model.annotationThickness, in: 1...32).frame(width: 85).help("Stroke thickness")
                        Button("Clear…") { model.finishText(); confirmClear = true }.disabled(model.document.annotationItems.isEmpty)
                    }
                    .onChange(of: model.annotationTool) { _ in model.finishText(); model.annotationDraft = nil }
                }
                HStack {
                    Text("Workspace").font(.headline)
                    Spacer()
                    Text(model.saving ? "Saving…" : model.loading ? "Opening cards…" : model.message)
                        .font(.caption).foregroundColor(.secondary)
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
                        } else {
                            Button("Retry saving") { model.save() }.disabled(model.saving)
                            Button("Reload saved card") { model.reloadSavedCard() }.disabled(model.saving)
                        }
                    }
                }
            }.padding(24).frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .alert("Clear all annotations?", isPresented: $confirmClear) {
            Button("Cancel", role: .cancel) {}
            Button("Clear annotations", role: .destructive) { model.clearAnnotations() }
        } message: { Text("This cannot be undone. Your cards stay in place.") }
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

    private func geometry(_ title: String, value: Double, range: ClosedRange<Double>, change: @escaping @MainActor @Sendable (Double) -> Void) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            Text(title).font(.caption)
            Slider(value: Binding(get: { value }, set: change), in: range).disabled(!model.editable)
        }
    }
    private func startRename(_ card: WorkspaceCard) {
        guard model.editable else { return }
        model.editingName = true
        model.select(card.id)
        name = card.name; renaming = card
    }
    private var validName: Bool {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        return !trimmed.isEmpty && trimmed.utf16.count <= 120
    }
    private func finishRename() {
        guard let card = renaming, validName else { return }
        model.editingName = false
        model.rename(card.id, to: name)
        renaming = nil
        renameFocused = false
    }
    private func cancelRename() {
        renaming = nil
        renameFocused = false
        model.editingName = false
    }
    private func row(_ card: WorkspaceCard) -> some View {
        HStack(spacing: 8) {
            Toggle("Show \(card.name)", isOn: Binding(get: { card.visible }, set: { model.setVisible(card.id, $0) }))
                .labelsHidden().toggleStyle(.checkbox).disabled(!model.editable)
            if renaming?.id == card.id {
                VStack(alignment: .leading, spacing: 3) {
                    TextField("Card name", text: $name)
                        .textFieldStyle(.roundedBorder)
                        .focused($renameFocused)
                        .onAppear { renameFocused = true }
                        .onSubmit { finishRename() }
                        .onExitCommand { cancelRename() }
                        .onChange(of: renameFocused) { focused in
                            if !focused, renaming?.id == card.id {
                                if validName { finishRename() } else { cancelRename() }
                            }
                        }
                        .help("Enter to save; Escape to cancel")
                    if !validName {
                        Text("Use 1–120 characters.")
                            .font(.caption).foregroundColor(.secondary)
                    }
                }.frame(maxWidth: .infinity)
            } else {
                Text(card.name).lineLimit(1).help("Double-click to rename " + card.name)
                    .frame(maxWidth: .infinity, alignment: .leading).contentShape(Rectangle())
                    .onTapGesture(count: 2) { startRename(card) }
                    .onTapGesture { model.select(card.id) }
            }
            Menu {
                Button("Rename") { startRename(card) }
                Button("Bring to front") { model.reorder(card.id, .front) }
                Button("Move forward") { model.reorder(card.id, .forward) }
                Button("Move backward") { model.reorder(card.id, .backward) }
                Button("Send to back") { model.reorder(card.id, .back) }
                if card.previousVersion != nil {
                    Button("Restore previous version") { model.restoreVersion(card.id) }
                }
                Divider()
                Button("Delete…") {
                    guard model.editable else { return }
                    model.editingName = true; deleting = card; showDelete = true
                }
            } label: {
                Image(systemName: "ellipsis")
                    .font(.system(size: 15, weight: .semibold))
                    .frame(width: 28, height: 24).contentShape(Rectangle())
            }
                .menuStyle(.borderlessButton).menuIndicator(.hidden).fixedSize()
                .help("Card actions").accessibilityLabel("Actions for " + card.name)
                .disabled(!model.editable)
        }.padding(8)
            .background(model.document.selectedID == card.id ? Color.accentColor.opacity(0.18) : Color.clear)
            .cornerRadius(6)
    }
}
