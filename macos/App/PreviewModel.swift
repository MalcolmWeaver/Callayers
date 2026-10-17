import AppKit
import Combine

@MainActor
final class PreviewModel: ObservableObject {
    let renderer: CLRendererBridge
    @Published private(set) var image: NSImage?
    @Published private(set) var ready = false
    @Published private(set) var busy = false
    @Published private(set) var shown = true
    @Published private(set) var message = "Opening the preview…"
    @Published private(set) var problem = ""
    @Published var checkerboard = true
    @Published var scale = 1.0 {
        didSet { renderer.setCardScale(scale) }
    }
    private var timer: Timer?
    private var html = ""
    private var savedState = "{}"
    private var request: UInt = 0
    private var hideAfterSave = false
    private let defaults = UserDefaults.standard

    init() {
        let resources = Bundle.main.resourceURL ?? Bundle.main.bundleURL
        renderer = CLRendererBridge(resources: resources)
        do {
            html = try String(contentsOf: resources.appendingPathComponent("counter-card.html"), encoding: .utf8)
            savedState = defaults.string(forKey: "rendererPreviewCardState") ?? "{}"
            _ = renderer.loadCard(html, state: savedState)
        } catch {
            problem = "The preview card is missing from the app. Rebuild the app to restore it."
        }
        timer = Timer(timeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in self?.tick() }
        }
        if let timer = timer { RunLoop.main.add(timer, forMode: .common) }
    }

    deinit { timer?.invalidate() }

    func reload() {
        guard ready && !busy else { return }
        hideAfterSave = false
        beginSave()
    }

    func setVisible(_ visible: Bool) {
        guard !busy && visible != shown else { return }
        if visible {
            shown = true
            message = "Restoring the card…"
            _ = renderer.loadCard(html, state: savedState)
        } else if ready {
            hideAfterSave = true
            beginSave()
        }
    }

    func resetPreview() {
        request = 0; busy = false; shown = true
        savedState = "{}"
        defaults.removeObject(forKey: "rendererPreviewCardState")
        problem = ""; message = "Opening a fresh card…"
        _ = renderer.loadCard(html, state: savedState)
    }

    private func beginSave() {
        request = renderer.beginSnapshot()
        busy = request != 0
        if busy { message = "Saving card values…" }
    }

    private func tick() {
        if renderer.tick() { image = renderer.image }
        if request != 0, let state = renderer.snapshot(request: request) {
            savedState = state
            defaults.set(state, forKey: "rendererPreviewCardState")
            request = 0; busy = false
            if hideAfterSave {
                renderer.clear(); image = nil; shown = false
                message = "Card hidden. Its values are saved."
            } else {
                _ = renderer.loadCard(html, state: state)
                message = "Restoring the saved card…"
            }
        }
        let currentProblem = renderer.problem
        if !currentProblem.isEmpty {
            problem = currentProblem; busy = false; request = 0
        }
        let currentReady = renderer.ready
        if currentReady && !ready { message = "Click the card, type a note, or change its size." }
        ready = currentReady
    }
}
