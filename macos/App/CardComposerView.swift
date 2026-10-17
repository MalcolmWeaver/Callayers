import SwiftUI
import UniformTypeIdentifiers
struct CardComposerView: View {
    @ObservedObject var model: WorkspaceModel
    @ObservedObject var generation: GenerationController
    @ObservedObject var auth: AuthController
    @State private var attachments: [TextAttachment] = []
    @State private var attachmentError = ""
    var body: some View {
        VStack(alignment:.leading,spacing:9) {
            Text(model.selected.map { "Edit “\($0.name)”" } ?? "New card").font(.headline).lineLimit(1)
            Text(model.selected == nil ? "Describe the card you want to create." : "Describe what you want to change.")
                .font(.caption).foregroundColor(.secondary)
            TextEditor(text:Binding(get:{model.prompt},set:{model.setPrompt($0)}))
                .font(.system(size:13)).frame(height:85).padding(4)
                .background(Color(white:0.10)).cornerRadius(6)
                .overlay(RoundedRectangle(cornerRadius:6).stroke(Color.white.opacity(0.15)))
                .accessibilityLabel("Card instructions").disabled(generation.busy || model.locked || !auth.allowed)
            ForEach(attachments) { attachment in
                HStack { Image(systemName:"doc.text"); Text(attachment.name).lineLimit(1); Spacer(); Button { attachments.removeAll {$0.id == attachment.id} } label: { Image(systemName:"xmark") } }
                    .font(.caption).disabled(generation.busy)
            }
            HStack {
                Button { addText() } label: { Image(systemName:"paperclip") }.help("Attach source text").disabled(generation.busy || attachments.count >= 8)
                Spacer()
                if generation.busy { ProgressView().controlSize(.small); Button("Cancel") { generation.cancel() } }
                else { Button("Send") { attachmentError = ""; model.generate(attachments:attachments) }.buttonStyle(.borderedProminent).tint(AppTheme.accent)
                    .disabled(!model.editable || !auth.allowance.enabled || auth.allowance.exhausted || (model.prompt.trimmingCharacters(in:.whitespacesAndNewlines).isEmpty && attachments.isEmpty)) }
            }
            if auth.allowance.exhausted { Text(auth.allowance.resetDescription).font(.caption).foregroundColor(.secondary) }
            if !auth.allowance.enabled { Text("AI creation is temporarily unavailable. You can still use saved cards, images and annotations.").font(.caption).foregroundColor(.secondary) }
            if !generation.message.isEmpty { Text(generation.message).font(.caption).foregroundColor(.secondary).textSelection(.enabled) }
            if !attachmentError.isEmpty { Text(attachmentError).font(.caption).foregroundColor(.red) }
        }.onChange(of:model.selected?.id) { _ in attachments = [] }
    }
    private func addText() {
        let panel = NSOpenPanel(); panel.allowedContentTypes = [.plainText,.utf8PlainText,.commaSeparatedText,.sourceCode]; panel.allowsMultipleSelection = true
        guard panel.runModal() == .OK else { return }
        do {
            var values = attachments
            for url in panel.urls {
                guard values.count < 8, let size = try url.resourceValues(forKeys:[.fileSizeKey]).fileSize, size <= 100000 else { throw APIError(status:400,code:"prompt_too_large") }
                let text = try String(contentsOf:url,encoding:.utf8)
                values.append(TextAttachment(name:String(url.lastPathComponent.prefix(80)),text:text))
            }
            _ = try GenerationContract.body(prompt:model.prompt,attachments:values,current:model.selected,id:UUID())
            attachments = values; attachmentError = ""
        } catch { attachmentError = error.localizedDescription }
    }
}
