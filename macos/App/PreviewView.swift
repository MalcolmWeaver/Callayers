import SwiftUI
import AppKit

// Preserve SwiftUI's window delegate while routing the close button through
// the same asynchronous save-before-quit path as Cmd-Q.
@MainActor
private final class WorkspaceCloseDelegate: NSObject, NSWindowDelegate {
    // AppKit invokes these Objective-C forwarding hooks on the main thread.
    nonisolated(unsafe) weak var original: NSWindowDelegate?
    init(original: NSWindowDelegate?) { self.original = original }
    func windowShouldClose(_ sender: NSWindow) -> Bool { NSApp.terminate(nil); return false }
    override func responds(to selector: Selector!) -> Bool {
        super.responds(to: selector) || (original?.responds(to: selector) ?? false)
    }
    override func forwardingTarget(for selector: Selector!) -> Any? { original }
}

@MainActor
struct PreviewView: NSViewRepresentable {
    @ObservedObject var model: WorkspaceModel
    func makeNSView(context: Context) -> CardPreviewView {
        let view = CardPreviewView(frame: .zero); view.model = model; return view
    }
    func updateNSView(_ view: CardPreviewView, context: Context) {
        view.model = model; view.needsDisplay = true
    }
}

@MainActor
final class CardPreviewView: NSView {
    weak var model: WorkspaceModel?
    private var mouseTracking: NSTrackingArea?
    private var closeDelegate: WorkspaceCloseDelegate?
    private var focusedID: UUID?
    private var pointerID: UUID?
    private var hoveredID: UUID?
    private var drag: (card: WorkspaceCard, start: CGPoint, resizing: Bool)?
    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        guard let window = window, window.delegate !== closeDelegate else { return }
        let delegate = WorkspaceCloseDelegate(original: window.delegate)
        closeDelegate = delegate; window.delegate = delegate
    }

    override func updateTrackingAreas() {
        if let area = mouseTracking { removeTrackingArea(area) }
        let area = NSTrackingArea(rect: .zero, options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect], owner: self, userInfo: nil)
        addTrackingArea(area); mouseTracking = area
        super.updateTrackingAreas()
    }
    private var sx: CGFloat { bounds.width / 1280 }
    private var sy: CGFloat { bounds.height / 720 }
    private func rect(_ card: WorkspaceCard) -> NSRect {
        NSRect(x: card.x * sx, y: card.y * sy, width: card.width * sx, height: card.height * sy)
    }
    private func handle(_ card: WorkspaceCard) -> NSRect {
        let r = rect(card); return NSRect(x: r.maxX - 5, y: r.maxY - 5, width: 10, height: 10)
    }
    override func draw(_ dirtyRect: NSRect) {
        NSColor(calibratedWhite: 0.16, alpha: 1).setFill(); bounds.fill()
        guard let model = model else { return }
        if let picture = model.video.preview {
            picture.draw(in: bounds, from: .zero, operation: .copy, fraction: 1, respectFlipped: true, hints: nil)
        } else if model.checkerboard {
            let tile: CGFloat = 24
            NSColor(calibratedWhite: 0.23, alpha: 1).setFill()
            for y in stride(from: CGFloat(0), to: bounds.height, by: tile) {
                for x in stride(from: CGFloat(0), to: bounds.width, by: tile) where (Int(x / tile) + Int(y / tile)) % 2 == 0 {
                    NSRect(x: x, y: y, width: tile, height: tile).fill()
                }
            }
        }
        for card in model.visibleCards where model.video.preview == nil {
            guard let image = model.renderers[card.id]?.image else { continue }
            // Place the card canvas natively; scaling never reloads its HTML.
            NSGraphicsContext.saveGraphicsState()
            NSBezierPath(rect: rect(card)).addClip()
            image.draw(in: NSRect(x: card.x * sx,
                y: card.y * sy, width: 1280 * card.scale * sx,
                height: 720 * card.scale * sy), from: .zero, operation: .sourceOver,
                fraction: 1, respectFlipped: true, hints: nil)
            NSGraphicsContext.restoreGraphicsState()
        }
        if let card = model.selected, card.visible {
            NSColor.controlAccentColor.setStroke()
            let outline = NSBezierPath(rect: rect(card).insetBy(dx: -3, dy: -3))
            outline.lineWidth = 2; outline.stroke()
            NSColor.controlAccentColor.setFill(); handle(card).fill()
        }
    }

    private func point(_ event: NSEvent) -> CGPoint {
        let p = convert(event.locationInWindow, from: nil)
        return CGPoint(x: p.x / max(sx, 0.001), y: p.y / max(sy, 0.001))
    }
    private func hit(_ point: CGPoint) -> WorkspaceCard? {
        model?.visibleCards.reversed().first {
            point.x >= $0.x && point.x < $0.x + $0.width && point.y >= $0.y && point.y < $0.y + $0.height
        }
    }
    private func forward(_ event: NSEvent, id: UUID?, type: Int, button: Int) {
        guard let model = model, let card = model.visibleCards.first(where: { $0.id == id }) else { return }
        let p = point(event)
        model.renderers[card.id]?.mouse(type, x: Int((p.x - card.x) / card.scale),
            y: Int((p.y - card.y) / card.scale), button: button)
    }
    override func mouseDown(with event: NSEvent) {
        // Periodic snapshots must not swallow clicks or keystrokes. A later
        // snapshot (including the final quit snapshot) captures newer input.
        guard let model = model, !model.locked, !model.loading, !model.closing else { return }
        window?.makeFirstResponder(self)
        let local = convert(event.locationInWindow, from: nil), p = point(event)
        if let card = model.selected, card.visible {
            let r = rect(card)
            let resizing = handle(card).insetBy(dx: -3, dy: -3).contains(local)
            if resizing || (r.insetBy(dx: -7, dy: -7).contains(local) && !r.insetBy(dx: 5, dy: 5).contains(local)) {
                model.renderers[card.id]?.blur(); focusedID = nil
                drag = (card, p, resizing); return
            }
        }
        let card = hit(p)
        model.select(card?.id); focusedID = card?.id; pointerID = card?.id
        forward(event, id: pointerID, type: 1, button: 1)
    }
    override func mouseDragged(with event: NSEvent) {
        if let drag = drag {
            let p = point(event)
            if drag.resizing {
                let delta = max((p.x - drag.start.x) / drag.card.canvasWidth, (p.y - drag.start.y) / drag.card.canvasHeight)
                model?.placeSelected(scale: drag.card.scale + delta)
            } else { model?.placeSelected(x: drag.card.x + p.x - drag.start.x, y: drag.card.y + p.y - drag.start.y) }
        } else { forward(event, id: pointerID, type: 0, button: 1) }
    }
    override func mouseUp(with event: NSEvent) {
        if drag != nil { drag = nil }
        else { forward(event, id: pointerID, type: 2, button: 1) }
        pointerID = nil
    }
    override func mouseMoved(with event: NSEvent) {
        let id = hit(point(event))?.id
        if hoveredID != id, let old = hoveredID { model?.renderers[old]?.mouse(0, x: -1, y: -1, button: 0) }
        hoveredID = id; forward(event, id: id, type: 0, button: 0)
    }
    override func mouseExited(with event: NSEvent) {
        if let id = hoveredID { model?.renderers[id]?.mouse(0, x: -1, y: -1, button: 0) }
        hoveredID = nil
    }
    override func scrollWheel(with event: NSEvent) {
        guard let card = hit(point(event)) else { return }
        let p = point(event), factor = event.hasPreciseScrollingDeltas ? 1.0 : 24.0
        model?.renderers[card.id]?.scroll(x: Int((p.x - card.x) / card.scale),
            y: Int((p.y - card.y) / card.scale), deltaX: Int(event.scrollingDeltaX * factor), deltaY: Int(event.scrollingDeltaY * factor))
    }
    override func keyDown(with event: NSEvent) {
        guard let id = focusedID, model?.document.selectedID == id, model?.closing == false else { return }
        model?.renderers[id]?.keyEvent(event)
    }
    override func keyUp(with event: NSEvent) {
        guard let id = focusedID, model?.document.selectedID == id else { return }
        model?.renderers[id]?.keyEvent(event)
    }
    override func resignFirstResponder() -> Bool {
        if let model = model { for renderer in model.renderers.values { renderer.blur() } }
        focusedID = nil
        return super.resignFirstResponder()
    }
}
