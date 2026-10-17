import SwiftUI

struct WorkspaceView: View {
    @ObservedObject var model: WorkspaceModel
    @ObservedObject var video: VideoModel
    @ObservedObject var annotations: AnnotationController
    @Binding var showCameraSetup: Bool
    @State private var search = ""
    @State private var renaming: UUID?
    @State private var name = ""
    @State private var deleting: WorkspaceCard?
    @State private var showDelete = false
    @State private var showAnnotations = false
    @State private var recover = false
    @FocusState private var renameFocused: Bool
    var body: some View {
        VStack(spacing:0) {
            HStack(spacing:14) {
                CallayersMark(); Text("Callayers").font(.system(size:24,weight:.semibold))
                Menu("File") { Button("Sign out") { model.auth.signOut() }; Divider(); Button("Quit") { NSApp.terminate(nil) } }
                Spacer()
                Button { showAnnotations.toggle(); if !showAnnotations { annotations.tool = .select } } label: { Label("Annotate",systemImage:"pencil") }
                    .tint(showAnnotations ? AppTheme.accent : nil).disabled(!model.editable)
                Picker("Webcam",selection:Binding(get:{video.selectedID},set:{video.choose($0)})) {
                    Text("Choose webcam…").tag("")
                    ForEach(video.cameras) { Text($0.name).tag($0.id) }
                }.labelsHidden().frame(width:235)
                Button("Camera setup…") { showCameraSetup = true }
            }.padding(.horizontal,24).padding(.vertical,18).background(AppTheme.sidebar)
            Divider()
            HStack(spacing:0) {
                VStack(alignment:.leading,spacing:13) {
                    Text("Cards").font(.title3.weight(.semibold))
                    Button { model.select(nil) } label: { Text("+  Create with AI").frame(maxWidth:.infinity).padding(.vertical,6) }
                        .buttonStyle(.borderedProminent).tint(AppTheme.accent).disabled(!model.editable)
                    HStack { Button("Import image") { model.importImage() }; Button("Hide all") { model.hideAll() }.disabled(model.visibleCards.isEmpty) }.disabled(!model.editable)
                    TextField("Search cards",text:$search).textFieldStyle(.roundedBorder)
                    ScrollView {
                        LazyVStack(spacing:4) {
                            ForEach(model.library.filter { search.isEmpty || $0.name.localizedCaseInsensitiveContains(search) }) { row($0) }
                            if model.library.isEmpty { Text("Create your first card").foregroundColor(.secondary).padding(.top,24) }
                            else if !model.library.contains(where:{search.isEmpty || $0.name.localizedCaseInsensitiveContains(search)}) { Text("No matching cards").foregroundColor(.secondary).padding(.top,24) }
                        }
                    }.frame(maxHeight:.infinity)
                    Divider()
                    CardComposerView(model:model,generation:model.generation,auth:model.auth)
                    Divider()
                    Button("Use in a call") { showCameraSetup = true }.frame(maxWidth:.infinity)
                    Text("In your calling app, choose Callayers.").font(.caption).foregroundColor(.secondary)
                    Text("Your cards save automatically on this computer.").font(.caption2).foregroundColor(.secondary)
                }.padding(18).frame(width:295).background(AppTheme.sidebar)
                Divider()
                VStack(spacing:14) {
                    if showAnnotations { AnnotationToolbar(controller:annotations) }
                    Spacer(minLength:0)
                    PreviewView(model:model).aspectRatio(16/9,contentMode:.fit).frame(maxWidth:.infinity)
                    Spacer(minLength:0)
                    if !model.rendererProblem.isEmpty {
                        Text(model.rendererProblem).font(.callout).foregroundColor(.red)
                        Button("Reload card") { model.reloadSavedCard() }.disabled(model.saving)
                    }
                    if !model.problem.isEmpty {
                        Text(model.problem).font(.callout).foregroundColor(.red).textSelection(.enabled)
                        HStack {
                            if model.locked { Button("Retry opening") { model.retryOpen() }; if model.canRecover { Button("Restore backup…") { recover = true } } }
                            else { Button("Retry saving") { model.save() }; if model.selected != nil { Button("Reload saved card") { model.reloadSavedCard() } } }
                        }
                    }
                    HStack { Text(video.cameraStatus); Spacer(); Text(model.saving ? "Saving…" : "") }.font(.caption).foregroundColor(.secondary)
                }.padding(24).frame(maxWidth:.infinity,maxHeight:.infinity)
            }
        }.background(AppTheme.background)
        .alert("Delete card?",isPresented:$showDelete,presenting:deleting) { card in
            Button("Cancel",role:.cancel) { model.editingName = false }
            Button("Delete",role:.destructive) { model.editingName = false; model.delete(card.id) }
        } message: { Text("This removes \($0.name) and its previous version. It cannot be restored.") }
        .onChange(of:showDelete) { if !$0 { model.editingName = false } }
        .alert("Restore the saved backup?",isPresented:$recover) { Button("Cancel",role:.cancel) { }; Button("Restore") { model.recoverBackup() } }
            message: { Text("The previous saved workspace will reopen. The unreadable original will be preserved.") }
    }
    private func startRename(_ card: WorkspaceCard) { guard model.editable else { return }; model.editingName = true; model.select(card.id); name = card.name; renaming = card.id; renameFocused = true }
    private func finishRename(cancel: Bool = false) {
        if !cancel, let id = renaming { model.rename(id,to:name) }
        renaming = nil; renameFocused = false; model.editingName = false
    }
    private func row(_ card: WorkspaceCard) -> some View {
        HStack(spacing:8) {
            Toggle("Show \(card.name)",isOn:Binding(get:{card.visible},set:{model.setVisible(card.id,$0)})).labelsHidden().toggleStyle(.checkbox).disabled(!model.editable)
            if renaming == card.id {
                TextField("Card name",text:$name).focused($renameFocused).onSubmit { finishRename() }.onExitCommand { finishRename(cancel:true) }
                    .onChange(of:renameFocused) { if !$0,renaming == card.id { finishRename() } }
            } else {
                Text(card.name).lineLimit(1).frame(maxWidth:.infinity,alignment:.leading).contentShape(Rectangle()).help(card.name)
                    .onTapGesture(count:2) { startRename(card) }.onTapGesture { model.select(card.id) }
            }
            Menu {
                Button("Rename") { startRename(card) }
                Button("Bring to front") { model.select(card.id); model.stackSelected(front:true) }
                Button("Send to back") { model.select(card.id); model.stackSelected(front:false) }
                if card.previous != nil { Button("Restore previous version") { model.select(card.id); model.restorePrevious() } }
                Divider(); Button("Delete…") { deleting = card; showDelete = true; model.editingName = true }
            } label: { Image(systemName:"ellipsis").frame(width:22,height:24) }.menuStyle(.borderlessButton).menuIndicator(.hidden).fixedSize().disabled(!model.editable)
        }.padding(8).background(model.document.selectedID == card.id ? AppTheme.accent.opacity(0.22) : .clear).cornerRadius(5)
    }
}
