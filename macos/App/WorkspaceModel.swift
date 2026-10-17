import AppKit
import Combine

@MainActor
final class WorkspaceModel: ObservableObject {
    @Published private(set) var document = WorkspaceDocument()
    @Published private(set) var saving = false
    @Published private(set) var locked = false
    @Published private(set) var problem = ""
    @Published private(set) var message = "Opening workspace…"
    @Published private(set) var frameSerial: UInt = 0
    @Published var checkerboard = true
    let video = VideoModel()
    var editingName = false
    private(set) var renderers: [UUID: CLRendererBridge] = [:]
    private var store: WorkspaceStore?
    private let resources = Bundle.main.resourceURL ?? Bundle.main.bundleURL
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
    var editable: Bool { !locked && !saving && !loading && !closing }

    init() {
        do { store = try WorkspaceStore.applicationStore(); try open() }
        catch { locked = true; problem = "Could not open your workspace. The saved file has been kept. \(error.localizedDescription)" }
        timer = Timer(timeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in self?.tick() }
        }
        if let timer = timer { RunLoop.main.add(timer, forMode: .common) }
    }
    deinit { timer?.invalidate() }

    private func open() throws {
        guard let store = store else { throw WorkspaceError.locked }
        document = try store.load()
        locked = false; problem = ""; message = "Workspace opened."
        syncRenderers(); nextSave = Date().addingTimeInterval(2)
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
            syncRenderers(); nextSave = Date().addingTimeInterval(2)
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
            saving = false; problem = ""; message = "Workspace saved."
            syncRenderers(); nextSave = Date().addingTimeInterval(2)
            if quitNeedsFreshSnapshot {
                quitNeedsFreshSnapshot = false; save()
            } else {
                let completion = quitCompletion; quitCompletion = nil; completion?(true)
            }
        } catch { failSave("Could not save your workspace. Changes remain open; try Save now. \(error.localizedDescription)") }
    }

    private func failSave(_ error: String) {
        pending = [:]; mutation = nil; saving = false
        problem = error.isEmpty ? "The card could not return its saved values. Reload the saved card to retry." : error
        nextSave = .distantFuture // Keep the error visible; an explicit retry is available.
        quitNeedsFreshSnapshot = false
        let completion = quitCompletion; quitCompletion = nil; completion?(false)
    }

    private func syncRenderers() {
        let visible = Set(visibleCards.map(\.id))
        for id in Array(renderers.keys) where !visible.contains(id) { renderers.removeValue(forKey: id) }
        for card in visibleCards where renderers[card.id] == nil {
            let renderer = CLRendererBridge(resources: resources)
            _ = renderer.loadCard(card.html, state: card.state)
            renderers[card.id] = renderer
        }
        frameSerial &+= 1
    }

    private func tick() {
        var changed = false
        for renderer in renderers.values { if renderer.tick() { changed = true } }
        let layers: [VideoLayer] = visibleCards.compactMap { card in
            guard let image = renderers[card.id]?.image?.cgImage(forProposedRect: nil, context: nil, hints: nil) else { return nil }
            // Renderer frames use a 1280x720 logical scene, with the card at
            // (160,120). Crop in pixel coordinates before native placement.
            let sx = CGFloat(image.width) / 1280, sy = CGFloat(image.height) / 720
            guard let cropped = image.cropping(to: CGRect(x: 160 * sx, y: 120 * sy, width: 640 * sx, height: 400 * sy)) else { return nil }
            return VideoLayer(image: cropped, rect: CGRect(x: card.x, y: card.y, width: card.width, height: card.height))
        }
        if video.tick(layers: layers) { changed = true }
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
        } else if !locked && !loading && !editingName && Date() >= nextSave { save() }
    }
}
