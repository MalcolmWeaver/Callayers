import AppKit
import Combine
import UniformTypeIdentifiers

@MainActor
final class WorkspaceModel: ObservableObject {
    @Published private(set) var document = WorkspaceDocument()
    @Published private(set) var saving = false
    @Published private(set) var locked = false
    @Published private(set) var problem = ""
    @Published private(set) var message = "Opening workspace…"
    @Published private(set) var frameSerial: UInt = 0
    @Published private(set) var importing = false
    @Published var checkerboard = true
    let video: VideoModel
    private var authorized = true
    weak var telemetry: TelemetrySession?
    private var pendingCreations: [(TelemetryEvent.Creation, UUID)] = []
    weak var auth: AuthController?
    @Published private(set) var generating = false
    @Published private(set) var generationMessage = ""
    private var generationTask: Task<Void, Never>?
    private var generationID: UUID?
    private let generator = GenerationService()
    @Published var annotationToolbar = false
    @Published var annotationTool: AnnotationTool = .select
    @Published var annotationColor: UInt32 = 0x56ddca
    @Published var annotationThickness = 4.0
    @Published var selectedAnnotation: UUID?
    @Published var annotationDraft: WorkspaceAnnotation?
    @Published var editingText = false
    private var renderedAnnotations: [WorkspaceAnnotation]?
    private var renderedDraftID: UUID?
    private var annotationImage: CGImage?
    private var editingBackgroundImage: CGImage?
    var editingName = false
    private(set) var renderers: [UUID: CLRendererBridge] = [:]
    private var store: WorkspaceStore?
    private let resources = Bundle.main.resourceURL ?? Bundle.main.bundleURL
    private var renderingPaused = false
    private var timer: Timer?
    private var pending: [UUID: UInt] = [:]
    private var mutation: ((inout WorkspaceDocument) -> Void)?
    private var nextSave = Date.distantFuture
    private var quitCompletion: ((Bool) -> Void)?
    private var quitNeedsFreshSnapshot = false

    var selected: WorkspaceCard? { document.cards.first { $0.id == document.selectedID } }
    var library: [WorkspaceCard] { document.cards.sorted { $0.edited > $1.edited } }
    var visibleCards: [WorkspaceCard] { document.cards.filter(\.visible) }
    var canRecover: Bool { store?.canRecoverBackup ?? false }
    var loading: Bool { renderers.values.contains { !$0.ready && $0.problem.isEmpty } }
    var closing: Bool { quitCompletion != nil }
    var editable: Bool { authorized && !locked && !saving && !loading && !closing && !importing }

    init(video: VideoModel? = nil, auth: AuthController? = nil) {
        self.video = video ?? VideoModel(); self.auth = auth
        do { store = try WorkspaceStore.applicationStore(); try open() }
        catch { locked = true; problem = "Could not open your workspace. The saved file has been kept. \(error.localizedDescription)" }
        timer = Timer(timeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in self?.tick() }
        }
        if let timer = timer { RunLoop.main.add(timer, forMode: .common) }
    }
    deinit { timer?.invalidate() }

    func setAuthorized(_ allowed: Bool) {
        if !allowed { finishText(); cancelGeneration() }
        authorized = allowed; video.setAuthorized(allowed)
        if !allowed { save() }
    }
    var prompt: String { document.promptDraft ?? "" }
    func setPrompt(_ text: String) {
        guard text.utf8.count <= 100_000 else { generationMessage = "Use up to 100 KB of text."; return }
        document.promptDraft = text; nextSave = Date().addingTimeInterval(0.5)
    }
    func cancelGeneration() {
        guard generating else { return }
        generationID = nil; generationTask?.cancel(); generationTask = nil; generating = false
        generationMessage = "Cancelled. Processing may already have used a generation."
    }
    func generate() {
        guard editable, !generating, let auth, let token = auth.token, let uid = auth.grant?.saved.uid else { return }
        let input = prompt, card = selected, ticket = GenerationTicket(card: selected)
        do { _ = try GenerationService.body(prompt: input, card: card, ticket: ticket) }
        catch { generationMessage = error.localizedDescription; return }
        generationID = ticket.id; generating = true; generationMessage = card == nil ? "Creating your card…" : "Editing your card…"
        generationTask = Task { [weak self] in
            guard let self else { return }
            do {
                let result = try await generator.generate(prompt: input, card: card, ticket: ticket, token: token)
                while saving && !closing && generationID == ticket.id { try await Task.sleep(nanoseconds: 20_000_000) }
                guard generationID == ticket.id, auth.token != nil, auth.grant?.saved.uid == uid, !closing, authorized else { return }
                save { doc in
                    guard self.generationID == ticket.id, self.authorized else { return }
                    do {
                        let id = try doc.apply(result, ticket: ticket)
                        self.renderers.removeValue(forKey: id)
                        if doc.promptDraft == input { doc.promptDraft = "" }
                        self.pendingCreations.append((ticket.target == nil ? .aiCreate : .aiRevision, id))
                        self.generationMessage = "Your card is ready."
                    } catch { self.generationMessage = error.localizedDescription }
                    self.generating = false; self.generationID = nil
                }
                if !saving && generating { generating = false; generationID = nil }
            } catch {
                guard generationID == ticket.id else { return }
                generating = false; generationID = nil
                generationMessage = error.localizedDescription
                if let failure = error as? GenerationFailure, failure.status == 401 || failure.status == 403 { auth.rejectSession() }
            }
        }
    }

    private func open() throws {
        guard let store = store else { throw WorkspaceError.locked }
        document = try store.load()
        locked = false; problem = ""; message = "Workspace opened."
        refreshAnnotations(); syncRenderers(); nextSave = Date().addingTimeInterval(2)
    }

    func retryOpen() {
        guard locked else { return }
        do {
            if store == nil { store = try WorkspaceStore.applicationStore() }
            try open()
        } catch { problem = "Could not open your workspace. The saved file has been kept. \(error.localizedDescription)" }
    }

    func recoverBackup() {
        guard locked, let store = store else { return }
        do {
            document = try store.recoverBackup(); locked = false; problem = ""
            message = "Restored the backup. The original file has been preserved."
            if !video.wantsFrames { renderingPaused = true }
            refreshAnnotations(); syncRenderers(); nextSave = Date().addingTimeInterval(2)
        } catch { problem = "Could not restore the backup: \(error.localizedDescription)" }
    }

    func select(_ id: UUID?) {
        guard !locked, !closing else { return }
        if document.selectedID != id {
            for renderer in renderers.values { renderer.blur() }
            document.selectedID = id
            nextSave = Date().addingTimeInterval(0.5)
        }
    }

    func placeSelected(x: Double? = nil, y: Double? = nil, scale: Double? = nil) {
        guard editable, let index = document.cards.firstIndex(where: { $0.id == document.selectedID }) else { return }
        document.cards[index].place(x: x, y: y, scale: scale)
        nextSave = Date().addingTimeInterval(0.5)
    }

    func rename(_ id: UUID, to name: String) {
        let name = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard editable else { return }
        guard !name.isEmpty, name.utf16.count <= 120 else {
            problem = "Use a card name between 1 and 120 characters."; return
        }
        save { doc in
            guard let i = doc.cards.firstIndex(where: { $0.id == id }) else { return }
            doc.cards[i].name = name; doc.cards[i].edited = Date()
        }
    }

    func importImage() {
        guard editable, document.cards.count < 100 else { return }
        let panel = NSOpenPanel()
        panel.title = "Import image"
        panel.allowedContentTypes = [.png, .jpeg, .bmp]
        panel.allowsMultipleSelection = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        importing = true
        let scoped = url.startAccessingSecurityScopedResource()
        Task {
            let result = await Task.detached(priority: .userInitiated) { Result { try ImageImport.read(url) } }.value
            if scoped { url.stopAccessingSecurityScopedResource() }
            while saving && !closing { try? await Task.sleep(nanoseconds: 20_000_000) }
            importing = false
            guard !closing else { return }
            do {
                let image = try result.get()
                guard let store else { throw WorkspaceError.locked }
                let assetID = UUID()
                try store.saveImage(image.png, id: assetID)
                var card = WorkspaceCard(name: image.name, html: ImportedImage.html)
                card.imageID = assetID
                card.contentWidth = Double(image.width); card.contentHeight = Double(image.height)
                card.place(scale: min(1, min(640 / card.canvasWidth, 400 / card.canvasHeight)))
                save { doc in doc.cards.append(card); doc.selectedID = card.id; self.pendingCreations.append((.imageImport, card.id)) }
            } catch { problem = error.localizedDescription }
        }
    }

    func addCounter() {
        guard editable, document.cards.count < 100 else { return }
        do {
            let html = try String(contentsOf: resources.appendingPathComponent("counter-card.html"), encoding: .utf8)
            save { doc in
                var card = WorkspaceCard(name: "Counter \(doc.cards.count + 1)", html: html)
                let offset = Double(doc.cards.count % 6) * 32
                card.place(x: 160 + offset, y: 120 + offset)
                doc.cards.append(card); doc.selectedID = card.id
            }
        } catch { problem = "The bundled counter could not be opened: \(error.localizedDescription)" }
    }

    func duplicateSelected() {
        guard editable, let id = document.selectedID, document.cards.count < 100 else { return }
        save { doc in
            guard var card = doc.cards.first(where: { $0.id == id }) else { return }
            card.id = UUID()
            while (card.name + " copy").utf16.count > 120 { card.name.removeLast() }
            card.name += " copy"; card.edited = Date()
            card.place(x: card.x + 24, y: card.y + 24)
            doc.cards.append(card); doc.selectedID = card.id
        }
    }

    func setVisible(_ id: UUID, _ visible: Bool) {
        guard editable else { return }
        save { doc in
            if let i = doc.cards.firstIndex(where: { $0.id == id }) { doc.cards[i].visible = visible }
        }
    }
    func hideAll() { guard editable else { return }; save { doc in for i in doc.cards.indices { doc.cards[i].visible = false } } }
    func delete(_ id: UUID) {
        guard editable else { return }
        save { doc in
            doc.cards.removeAll { $0.id == id }
            if doc.selectedID == id { doc.selectedID = nil }
        }
    }
    func bringToFront() {
        guard editable, let id = document.selectedID else { return }
        save { doc in
            guard let i = doc.cards.firstIndex(where: { $0.id == id }) else { return }
            let card = doc.cards.remove(at: i); doc.cards.append(card)
        }
    }

    func reorder(_ id: UUID, _ order: CardOrder) {
        guard editable else { return }
        save { $0.reorder(id, order) }
    }
    func restoreVersion(_ id: UUID) {
        guard editable else { return }
        save { doc in
            guard let index = doc.cards.firstIndex(where: { $0.id == id }), doc.cards[index].restorePreviousVersion() else { return }
            self.renderers.removeValue(forKey: id)
        }
    }

    func refreshAnnotations() {
        let items = document.annotationItems, draftID = annotationDraft?.id
        if renderedAnnotations != items {
            annotationImage = AnnotationRenderer.image(items)
        }
        if renderedAnnotations != items || renderedDraftID != draftID {
            editingBackgroundImage = draftID == nil ? nil : AnnotationRenderer.image(items.filter { $0.id != draftID })
        }
        renderedAnnotations = items; renderedDraftID = draftID
        frameSerial &+= 1
    }
    func commitAnnotation(_ item: WorkspaceAnnotation) {
        guard !locked, !closing, item.valid else { return }
        var items = document.annotationItems
        if let index = items.firstIndex(where: { $0.id == item.id }) { items[index] = item }
        else {
            guard items.count < 500 else { problem = "You have reached the annotation limit."; return }
            items.append(item); pendingCreations.append((.annotation, item.id))
        }
        guard items.reduce(0, { $0 + $1.points.count }) <= 200_000 else { problem = "These annotations are too large to save."; return }
        document.annotationItems = items
        selectedAnnotation = item.id; refreshAnnotations(); save()
    }
    func removeAnnotation(_ id: UUID) {
        guard !locked, !closing else { return }
        document.annotationItems.removeAll { $0.id == id }
        if selectedAnnotation == id { selectedAnnotation = nil }
        refreshAnnotations(); save()
    }
    func clearAnnotations() {
        guard !locked, !closing else { return }
        cancelText(); selectedAnnotation = nil; document.annotationItems = []
        refreshAnnotations(); save()
    }
    func setAnnotationColor(_ color: UInt32) {
        annotationColor = color
        if var draft = annotationDraft { draft.color = color; annotationDraft = draft }
        else if var item = document.annotationItems.first(where: { $0.id == selectedAnnotation }) {
            item.color = color; commitAnnotation(item)
        }
    }
    func beginText(_ item: WorkspaceAnnotation) {
        annotationDraft = item; editingText = true; selectedAnnotation = item.id
        refreshAnnotations()
    }
    func finishText() {
        guard editingText, let item = annotationDraft else { return }
        editingText = false; annotationDraft = nil
        if !item.text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty { commitAnnotation(item) }
        else if document.annotationItems.contains(where: { $0.id == item.id }) { removeAnnotation(item.id) }
        refreshAnnotations()
    }
    func cancelText() {
        editingText = false; annotationDraft = nil; refreshAnnotations()
    }

    func reloadSavedCard() {
        guard !locked, !saving, let id = document.selectedID else { return }
        renderers.removeValue(forKey: id)
        problem = ""; syncRenderers()
    }

    func save(_ change: ((inout WorkspaceDocument) -> Void)? = nil) {
        guard !locked, !saving else { return }
        saving = true; mutation = change; pending = [:]
        for card in visibleCards {
            guard let renderer = renderers[card.id] else { continue }
            if !renderer.ready {
                // A still-opening card has received no input. Its existing
                // saved state is safe to keep, including when quitting now.
                if renderer.problem.isEmpty { continue }
                failSave(renderer.problem)
                return
            }
            let request = renderer.beginSnapshot()
            if request == 0 { failSave(renderer.problem); return }
            pending[card.id] = request
        }
        if pending.isEmpty { finishSave() }
    }

    func saveBeforeQuit(_ completion: @escaping (Bool) -> Void) {
        if locked { completion(true); return } // No workspace changes were permitted.
        quitCompletion = completion
        if saving { quitNeedsFreshSnapshot = true }
        else { save() }
    }

    private func finishSave() {
        mutation?(&document); mutation = nil
        do {
            guard let store = store else { throw WorkspaceError.locked }
            try store.save(document)
            for (kind, id) in pendingCreations {
                if kind == .annotation {
                    if document.annotationItems.contains(where: { $0.id == id }) { telemetry?.created(.annotation) }
                } else if document.cards.contains(where: { $0.id == id }) { telemetry?.created(kind, card: id) }
            }
            pendingCreations = []
            saving = false; problem = ""; message = "Workspace saved."
            if !video.wantsFrames { renderingPaused = true }
            refreshAnnotations(); syncRenderers(); nextSave = Date().addingTimeInterval(2)
            if quitNeedsFreshSnapshot {
                quitNeedsFreshSnapshot = false; save()
            } else {
                let completion = quitCompletion; quitCompletion = nil; completion?(true)
            }
        } catch { syncRenderers(); failSave("Could not save your workspace. Changes remain open; choose Retry saving. \(error.localizedDescription)") }
    }

    private func failSave(_ error: String) {
        pending = [:]; mutation = nil; saving = false
        if generating { cancelGeneration(); generationMessage = "The card could not be saved. Retry saving before sending again." }
        if !authorized { renderers.removeAll(); renderingPaused = true }
        problem = error.isEmpty ? "The card could not return its saved values. Reload the saved card to retry." : error
        nextSave = .distantFuture // Keep the error visible; an explicit retry is available.
        quitNeedsFreshSnapshot = false
        let completion = quitCompletion; quitCompletion = nil; completion?(false)
    }

    private func syncRenderers() {
        if renderingPaused { renderers.removeAll(); frameSerial &+= 1; return }
        let visible = Set(visibleCards.map(\.id))
        for id in Array(renderers.keys) where !visible.contains(id) { renderers.removeValue(forKey: id) }
        for card in visibleCards where renderers[card.id] == nil {
            let renderer = CLRendererBridge(resources: resources)
            do {
                guard let store else { throw WorkspaceError.locked }
                _ = renderer.loadCard(try store.renderedHTML(for: card), state: card.state,
                                      width: card.canvasWidth, height: card.canvasHeight)
            } catch { problem = "Couldn't open “\(card.name)”: \(error.localizedDescription)"; continue }
            renderers[card.id] = renderer
        }
        frameSerial &+= 1
    }

    private func tick() {
        video.refreshDemand()
        if !authorized && !saving { return }
        if renderingPaused && video.wantsFrames { renderingPaused = false; syncRenderers() }
        if !renderingPaused && !video.wantsFrames && !saving && !locked { save() }
        var changed = false
        for renderer in renderers.values { if renderer.tick() { changed = true } }
        var layers: [VideoLayer] = visibleCards.compactMap { card in
            guard let image = renderers[card.id]?.image?.cgImage(forProposedRect: nil, context: nil, hints: nil) else { return nil }
            // Crop the card canvas from the renderer before native placement.
            let sx = CGFloat(image.width) / 1280, sy = CGFloat(image.height) / 720
            guard let cropped = image.cropping(to: CGRect(x: 0, y: 0, width: card.canvasWidth * sx, height: card.canvasHeight * sy)) else { return nil }
            return VideoLayer(image: cropped, rect: CGRect(x: card.x, y: card.y, width: card.width, height: card.height), cardID: card.id, cardType: card.imageID == nil ? "ai" : "image")
        }
        let cardLayers = layers
        if let annotationImage { layers.append(VideoLayer(image: annotationImage, rect: CGRect(x: 0, y: 0, width: 1280, height: 720))) }
        var previewLayers: [VideoLayer]?
        if annotationDraft != nil {
            previewLayers = cardLayers
            if let editingBackgroundImage { previewLayers?.append(VideoLayer(image: editingBackgroundImage, rect: CGRect(x: 0, y: 0, width: 1280, height: 720))) }
        }
        if video.tick(layers: layers, previewLayers: previewLayers) { changed = true }
        if changed { frameSerial &+= 1 }
        if saving {
            for (id, request) in Array(pending) {
                guard let renderer = renderers[id] else { failSave("A card disappeared while saving."); return }
                if let state = renderer.snapshot(request: request) {
                    guard WorkspaceDocument.validState(state) else { failSave("A card returned invalid saved values."); return }
                    if let i = document.cards.firstIndex(where: { $0.id == id }) { document.cards[i].state = state }
                    pending.removeValue(forKey: id)
                } else if !renderer.problem.isEmpty { failSave(renderer.problem); return }
            }
            if pending.isEmpty { finishSave() }
        } else if !locked && !loading && !editingName && !importing && Date() >= nextSave { save() }
    }
}
