import AppKit
import Combine
import UniformTypeIdentifiers

@MainActor final class WorkspaceModel: ObservableObject {
    @Published private(set) var document = WorkspaceDocument()
    @Published private(set) var saving = false
    @Published private(set) var locked = false
    @Published private(set) var problem = ""
    @Published private(set) var message = "Opening workspace…"
    @Published private(set) var frameSerial: UInt = 0
    let video = VideoModel()
    let auth = AuthController()
    let generation = GenerationController()
    let annotations = AnnotationController()
    private let telemetry = Telemetry()
    private var nextRenewalCheck = Date.distantPast
    var editingName = false
    private(set) var renderers: [UUID:CLRendererBridge] = [:]
    private var rendererRevisions: [UUID:UUID] = [:]
    private var store: WorkspaceStore?
    private let disk = DispatchQueue(label:"com.callayers.workspace",qos:.utility)
    private let resources = Bundle.main.resourceURL ?? Bundle.main.bundleURL
    private var timer: Timer?
    private var observations = Set<AnyCancellable>()
    private var pending: [UUID:UInt] = [:]
    private var mutation: ((inout WorkspaceDocument)->Void)?
    private var nextSave = Date.distantFuture
    private var quitCompletion: ((Bool)->Void)?
    private var quitNeedsFreshSnapshot = false
    private var runtimeActive = false
    private var annotationImage: CGImage?
    private var afterSave: [()->Void] = []
    var onCreated: ((String,UUID?)->Void)?
    var selected: WorkspaceCard? { document.cards.first {$0.id == document.selectedID} }
    var library: [WorkspaceCard] { document.cards.sorted {$0.edited > $1.edited} }
    var visibleCards: [WorkspaceCard] { document.cards.filter(\.visible) }
    var canRecover: Bool { store?.canRecoverBackup ?? false }
    var loading: Bool { renderers.values.contains {!$0.ready && $0.problem.isEmpty} }
    var closing: Bool { quitCompletion != nil }
    var editable: Bool { auth.allowed && video.cameraReady && !locked && !saving && !closing }
    var prompt: String { document.prompt }
    init() {
        do { store = try WorkspaceStore.applicationStore(); try open() }
        catch { locked = true; problem = "Couldn't open your saved workspace. Your files have been kept. Retry opening or restore the backup." }
        annotations.changed = { [weak self] items, created in
            guard let self, !self.locked else { return }
            self.document.annotations = items; self.annotationImage = AnnotationRenderer.render(items)
            self.nextSave = Date().addingTimeInterval(0.3)
            if created { let epoch = self.auth.epoch; self.afterSave.append { [weak self] in guard let self,self.auth.epoch == epoch else { return }; self.onCreated?("annotation",nil) } }
        }
        auth.$allowed.removeDuplicates().sink { [weak self] allowed in
            guard let self else { return }
            self.video.setSessionAllowed(allowed)
            if !allowed { self.generation.cancel(); self.runtimeActive = false; self.save() }
            self.objectWillChange.send()
        }.store(in:&observations)
        timer = Timer(timeInterval:1.0/30,repeats:true) { [weak self] _ in Task { @MainActor [weak self] in self?.tick() } }
        if let timer { RunLoop.main.add(timer,forMode:.common) }
        onCreated = { [weak self] kind,id in self?.telemetry.created(kind:kind,id:id) }
        auth.restore()
    }
    deinit { timer?.invalidate() }
    private func open() throws {
        guard let store else { throw WorkspaceError.locked }
        document = try store.load(); annotations.load(document.annotations)
        annotationImage = AnnotationRenderer.render(document.annotations)
        locked = false; problem = ""; message = "Cards save automatically on this computer."
        nextSave = .distantFuture
    }
    func retryOpen() {
        guard locked else { return }
        do { if store == nil { store = try WorkspaceStore.applicationStore() }; try open() }
        catch { problem = "Couldn't open your workspace. Your saved files have been kept." }
    }
    func recoverBackup() {
        guard locked, let store else { return }
        do { document = try store.recoverBackup(); annotations.load(document.annotations); annotationImage = AnnotationRenderer.render(document.annotations)
            locked = false; problem = ""; nextSave = .distantFuture
        } catch { problem = "Couldn't restore the saved backup. Your files have been kept." }
    }
    func select(_ id: UUID?) {
        guard !locked, !closing else { return }
        if document.selectedID != id {
            renderers.values.forEach {$0.blur()}; document.selectedID = id; nextSave = Date().addingTimeInterval(0.5)
        }
    }
    func setPrompt(_ value: String) { document.prompt = value; nextSave = Date().addingTimeInterval(1) }
    func placeSelected(x: Double? = nil,y: Double? = nil,scale: Double? = nil) {
        guard editable, let i = document.cards.firstIndex(where:{$0.id == document.selectedID}) else { return }
        document.cards[i].place(x:x,y:y,scale:scale); nextSave = Date().addingTimeInterval(0.5)
    }
    func rename(_ id: UUID,to name: String) {
        let name = name.trimmingCharacters(in:.whitespacesAndNewlines)
        guard editable, !name.isEmpty, name.utf16.count <= 120, !name.unicodeScalars.contains(where:{$0.value < 32}) else { return }
        save { doc in
            guard let i = doc.cards.firstIndex(where:{$0.id == id}) else { return }
            doc.cards[i].name = name; doc.cards[i].renamed = true; doc.cards[i].revision = UUID(); doc.cards[i].edited = Date()
        }
    }
    func setVisible(_ id: UUID,_ visible: Bool) { guard editable else { return }; save { doc in if let i = doc.cards.firstIndex(where:{$0.id == id}) { doc.cards[i].visible = visible } } }
    func hideAll() { guard editable else { return }; save { doc in for i in doc.cards.indices { doc.cards[i].visible = false } } }
    func delete(_ id: UUID) { guard editable else { return }; save { doc in doc.cards.removeAll {$0.id == id}; if doc.selectedID == id { doc.selectedID = nil } } }
    func stackSelected(front: Bool) {
        guard editable, let id = document.selectedID else { return }
        save { doc in guard let i = doc.cards.firstIndex(where:{$0.id == id}) else { return }; let card = doc.cards.remove(at:i); doc.cards.insert(card,at:front ? doc.cards.count : 0) }
    }
    func restorePrevious() {
        guard editable, let id = document.selectedID else { return }
        save { doc in guard let i = doc.cards.firstIndex(where:{$0.id == id}) else { return }; _ = CardRevision.restore(&doc.cards[i]) }
    }
    func reloadSavedCard() { guard !locked,!saving,let id = document.selectedID else { return }; renderers[id] = nil; rendererRevisions[id] = nil; problem = ""; syncRenderers() }
    func importImage(url: URL? = nil) {
        guard editable,document.cards.count < 100 else { return }
        var source = url
        if source == nil {
            let panel = NSOpenPanel(); panel.allowedContentTypes = [.png,.jpeg,.bmp]; panel.allowsMultipleSelection = false
            guard panel.runModal() == .OK else { return }; source = panel.url
        }
        guard let source else { return }
        let epoch = auth.epoch
        Task {
            do {
                let content = try await Task.detached(priority:.userInitiated) { try ImageImport.prepare(url:source) }.value
                _ = await applyContent(id:nil,revision:nil,content:content,epoch:epoch,kind:"image_import")
            } catch { problem = error.localizedDescription }
        }
    }
    func generate(attachments: [TextAttachment]) {
        let epoch = auth.epoch
        generation.start(prompt:document.prompt,attachments:attachments,card:selected,auth:auth) { [weak self] id,revision,content in
            guard let self else { return false }
            return await self.applyContent(id:id,revision:revision,content:content,epoch:epoch,kind:id == nil ? "ai_create" : "ai_revision")
        }
    }
    func applyContent(id: UUID?,revision: UUID?,content: CardContent,epoch: UUID,kind: String) async -> Bool {
        for _ in 0..<100 { if !saving { break }; try? await Task.sleep(nanoseconds:50_000_000) }
        guard !Task.isCancelled, editable,auth.epoch == epoch,content.valid() else { return false }
        if let id { guard document.cards.contains(where:{$0.id == id && $0.revision == revision}) else { return false } }
        else if document.cards.count >= 100 { problem = "Your library has 100 cards. Remove a card before adding another."; return false }
        let createdID = id ?? UUID()
        afterSave.append { [weak self] in guard let self,self.auth.epoch == epoch else { return }; self.onCreated?(kind,createdID) }
        save { doc in
            if let id,let revision,let i = doc.cards.firstIndex(where:{$0.id == id}) {
                _ = CardRevision.apply(to:&doc.cards[i],expectedRevision:revision,content:content)
            } else {
                var card = WorkspaceCard(name:content.name,html:content.html); card.id = createdID
                card.image = content.image; card.contentWidth = content.width; card.contentHeight = content.height
                card.scale = min(1,480/content.width,340/content.height); card.visible = false; card.place(x:48,y:48)
                doc.cards.append(card)
            }
            doc.selectedID = createdID; doc.prompt = ""
        }
        return true
    }
    func save(_ change: ((inout WorkspaceDocument)->Void)? = nil) {
        guard !locked,!saving else { return }
        saving = true; mutation = change; pending = [:]
        for card in document.cards {
            guard let renderer = renderers[card.id],rendererRevisions[card.id] == card.revision else { continue }
            if !renderer.ready { continue }
            let request = renderer.beginSnapshot()
            if request == 0 { failSave("Couldn't save this card. Reload the saved card and retry."); return }; pending[card.id] = request
        }
        if pending.isEmpty { finishSave() }
    }
    func saveBeforeQuit(_ completion: @escaping (Bool)->Void) {
        if locked { completion(true); return }
        quitCompletion = completion
        if saving { quitNeedsFreshSnapshot = true } else { save() }
    }
    private func finishSave() {
        guard !writing else { return }; writing = true
        mutation?(&document); mutation = nil
        guard let store else { failSave("Couldn't save your workspace. Retry."); return }
        let snapshot = document
        let events = afterSave; afterSave = []
        // Keep completion separate from snapshot requests: only one write is in flight.
        pending = [:]
        disk.async { [weak self] in
            let result = Result { try store.save(snapshot) }
            DispatchQueue.main.async {
                guard let self else { return }
                switch result {
                case .success:
                    self.saving = false; self.writing = false; self.problem = ""; self.syncRenderers()
                    self.nextSave = self.document == snapshot ? Date().addingTimeInterval(2) : Date().addingTimeInterval(0.1)
                    events.forEach {$0()}
                    if self.quitNeedsFreshSnapshot { self.quitNeedsFreshSnapshot = false; self.save() }
                    else if self.quitCompletion != nil {
                        Task { @MainActor in
                            await self.telemetry.finish(auth:self.auth)
                            let completion = self.quitCompletion; self.quitCompletion = nil; completion?(true)
                        }
                    }
                case .failure: self.afterSave.insert(contentsOf:events,at:0); self.failSave("Couldn't save this card. Changes remain open. Retry saving before quitting.")
                }
            }
        }
    }
    private var writing = false
    private func failSave(_ error: String) {
        pending = [:]; mutation = nil; saving = false; writing = false; problem = error; nextSave = .distantFuture; syncRenderers()
        quitNeedsFreshSnapshot = false; let completion = quitCompletion; quitCompletion = nil; completion?(false)
    }
    private func syncRenderers() {
        let visible = runtimeActive ? Set(visibleCards.map(\.id)) : []
        for id in Array(renderers.keys) where !visible.contains(id) || document.cards.first(where:{$0.id == id})?.revision != rendererRevisions[id] {
            renderers[id] = nil; rendererRevisions[id] = nil
        }
        guard runtimeActive else { return }
        for card in visibleCards where renderers[card.id] == nil {
            let renderer = CLRendererBridge(resources:resources)
            let html = card.image.map { card.html.replacingOccurrences(of:"callayers-image:local",with:$0) } ?? card.html
            _ = renderer.loadCard(html,state:card.state,width:UInt(card.contentWidth),height:UInt(card.contentHeight))
            renderers[card.id] = renderer; rendererRevisions[card.id] = card.revision
        }
        frameSerial &+= 1
    }
    private func tick() {
        var changed = false
        for renderer in renderers.values { if renderer.tick() { changed = true } }
        let cardLayers: [VideoLayer] = runtimeActive ? visibleCards.compactMap { card in
            guard let renderer = renderers[card.id], renderer.ready,
                  let image = renderer.image?.cgImage(forProposedRect:nil,context:nil,hints:nil),
                  let crop = image.cropping(to:CGRect(x:0,y:0,width:card.contentWidth*1.5,height:card.contentHeight*1.5)) else { return nil }
            return VideoLayer(image:crop,rect:CGRect(x:card.x,y:card.y,width:card.width,height:card.height))
        } : []
        var layers = cardLayers
        if runtimeActive,let annotationImage { layers.append(VideoLayer(image:annotationImage,rect:CGRect(x:0,y:0,width:1280,height:720))) }
        var previewLayers: [VideoLayer]?
        if runtimeActive, let editing = annotations.editing {
            previewLayers = cardLayers
            if let image = AnnotationRenderer.render(document.annotations.filter { $0.id != editing.id }) {
                previewLayers?.append(VideoLayer(image:image,rect:CGRect(x:0,y:0,width:1280,height:720)))
            }
        }
        let readyCards = Dictionary(uniqueKeysWithValues:visibleCards.filter { renderers[$0.id]?.ready == true }.map { ($0.id,$0.kind) })
        if video.tick(layers:layers,previewLayers:previewLayers,cards:readyCards) { changed = true }
        telemetry.observe(auth:auth,state:VideoTelemetryState(preview:video.previewVisible && runtimeActive,
            connected:video.consumerCount > 0,active:video.outputActive,app:video.consumerCount > 0 ? video.consumerApp : "none"),
            preview:video.previewVisible && runtimeActive ? readyCards : [:],
            output:video.deliveredCards)
        if Date() >= nextRenewalCheck { nextRenewalCheck = Date().addingTimeInterval(30); auth.maintain() }
        let active = WorkspaceLifecycle.shouldRender(allowed:auth.allowed,camera:video.cameraReady,visible:video.previewVisible,consumer:video.consumerCount > 0,locked:locked)
        if active != runtimeActive {
            runtimeActive = active
            if active { syncRenderers() } else if !saving { save() }
        }
        if changed { frameSerial &+= 1 }
        if saving && !writing {
            for (id,request) in Array(pending) {
                guard let renderer = renderers[id] else { failSave("Couldn't save this card. Retry."); return }
                if let state = renderer.snapshot(request:request) {
                    guard WorkspaceDocument.validState(state) else { failSave("A card returned invalid saved values. Reload it and retry."); return }
                    if let i = document.cards.firstIndex(where:{$0.id == id}) { document.cards[i].state = state }
                    pending[id] = nil
                } else if !renderer.problem.isEmpty { failSave("Couldn't save this card. Reload the saved card and retry."); return }
            }
            if pending.isEmpty { finishSave() }
        } else if !saving && !locked && !loading && !editingName && Date() >= nextSave { save() }
    }
}
