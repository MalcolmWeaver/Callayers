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
        view.model = model; view.synchronizeTextEditor(); view.needsDisplay = true
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
    private var annotationDrag: (item: WorkspaceAnnotation, start: CGPoint, resizing: Bool)?
    private var eraseTarget: UUID?
    private var textEditor: AnnotationTextView?
    private var drag: (card: WorkspaceCard, start: CGPoint, resizing: Bool)?
    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        guard let window = window, window.delegate !== closeDelegate else { return }
        window.setFrameAutosaveName("CallayersWorkspace")
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
        drawAnnotationControls()
        if let card = model.selected, card.visible {
            NSColor.controlAccentColor.setStroke()
            let outline = NSBezierPath(rect: rect(card).insetBy(dx: -3, dy: -3))
            outline.lineWidth = 2; outline.stroke()
            NSColor.controlAccentColor.setFill(); handle(card).fill()
        }
    }

    func synchronizeTextEditor() {
        guard let model, model.editingText, let draft = model.annotationDraft else {
            textEditor?.removeFromSuperview(); textEditor = nil; return
        }
        let editor: AnnotationTextView
        if let existing = textEditor { editor = existing }
        else {
            editor = AnnotationTextView(frame: .zero)
            editor.owner = self; editor.delegate = self
            editor.drawsBackground = false; editor.isRichText = false; editor.importsGraphics = false
            editor.isAutomaticQuoteSubstitutionEnabled = false
            editor.textContainerInset = .zero; editor.textContainer?.lineFragmentPadding = 0
            editor.isHorizontallyResizable = false; editor.isVerticallyResizable = false
            editor.allowsUndo = true; editor.string = draft.text
            textEditor = editor; addSubview(editor)
        }
        let r = draft.bounds
        editor.frame = NSRect(x: r.minX * sx, y: r.minY * sy, width: r.width * sx, height: r.height * sy)
        editor.textContainer?.containerSize = editor.bounds.size
        editor.font = NSFont.systemFont(ofSize: draft.fontSize * sx)
        editor.textColor = AnnotationRenderer.color(draft.color)
        if window?.firstResponder !== editor { window?.makeFirstResponder(editor) }
    }
    func endText(cancel: Bool) {
        if cancel { model?.cancelText() } else { model?.finishText() }
        window?.makeFirstResponder(self); synchronizeTextEditor()
    }
    private func annotationAt(_ point: CGPoint) -> WorkspaceAnnotation? {
        model?.document.annotationItems.reversed().first { $0.contains(point) }
    }
    private func drawAnnotationControls() {
        guard let model, model.annotationToolbar else { return }
        if let draft = model.annotationDraft, !model.editingText, let image = AnnotationRenderer.image([draft]) {
            NSImage(cgImage: image, size: NSSize(width: 1280, height: 720)).draw(in: bounds, from: .zero,
                operation: .sourceOver, fraction: 1, respectFlipped: true, hints: nil)
        }
        let selected = model.annotationDraft ?? model.document.annotationItems.first { $0.id == (eraseTarget ?? model.selectedAnnotation) }
        if let selected {
            let r = selected.bounds
            let rect = NSRect(x: r.minX * sx, y: r.minY * sy, width: r.width * sx, height: r.height * sy)
            (eraseTarget == nil ? NSColor.cyan : NSColor.systemRed).setStroke()
            let outline = NSBezierPath(rect: rect.insetBy(dx: -3, dy: -3)); outline.lineWidth = 2; outline.stroke()
            if !model.editingText {
                NSColor.cyan.setFill(); NSRect(x: rect.maxX - 5, y: rect.maxY - 5, width: 10, height: 10).fill()
            }
        }
    }
    private func annotationMouseDown(_ event: NSEvent, point: CGPoint) -> Bool {
        guard let model, model.annotationToolbar else { return false }
        let tool = model.annotationTool
        if tool == .eraser {
            if let item = annotationAt(point) { model.removeAnnotation(item.id) }
            eraseTarget = nil; needsDisplay = true; return true
        }
        if tool == .select {
            if let selected = model.document.annotationItems.first(where: { $0.id == model.selectedAnnotation }),
               NSRect(x: selected.bounds.maxX - 8 / sx, y: selected.bounds.maxY - 8 / sy, width: 16 / sx, height: 16 / sy).contains(point) {
                annotationDrag = (selected, point, true); model.annotationDraft = selected; model.refreshAnnotations(); return true
            }
            if let item = annotationAt(point) {
                model.select(nil); model.selectedAnnotation = item.id
                if item.kind == .text && event.clickCount == 2 { model.beginText(item); synchronizeTextEditor() }
                else { annotationDrag = (item, point, false); model.annotationDraft = item; model.refreshAnnotations() }
                needsDisplay = true; return true
            }
            model.selectedAnnotation = nil; return false
        }
        model.select(nil); model.selectedAnnotation = nil
        let start = AnnotationPoint(point)
        var item = WorkspaceAnnotation(kind: tool, points: [start, start], color: model.annotationColor, thickness: model.annotationThickness)
        if tool == .text {
            let origin = CGPoint(x: min(start.x, 960), y: min(start.y, 580))
            item.points = [AnnotationPoint(origin), AnnotationPoint(CGPoint(x: origin.x + 320, y: origin.y + 140))]
            model.beginText(item); synchronizeTextEditor()
        } else { model.annotationDraft = item; model.refreshAnnotations() }
        needsDisplay = true; return true
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
        if model.editingText { endText(cancel: false) }
        window?.makeFirstResponder(self)
        if annotationMouseDown(event, point: point(event)) { return }
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
        if let model, let moved = annotationDrag {
            let p = point(event)
            if moved.resizing {
                let r = moved.item.bounds
                model.annotationDraft = moved.item.resized(factor: 1 + ((p.x - moved.start.x) * r.width + (p.y - moved.start.y) * r.height) / (r.width * r.width + r.height * r.height))
            } else { model.annotationDraft = moved.item.moved(dx: p.x - moved.start.x, dy: p.y - moved.start.y) }
            needsDisplay = true; return
        }
        if let model, var draft = model.annotationDraft, !model.editingText {
            if draft.kind == .pen {
                if draft.points.count < 8192 { draft.points.append(AnnotationPoint(point(event))) }
            } else { draft.points[1] = AnnotationPoint(point(event)) }
            model.annotationDraft = draft; needsDisplay = true; return
        }
        if let drag = drag {
            let p = point(event)
            if drag.resizing {
                let delta = max((p.x - drag.start.x) / drag.card.canvasWidth, (p.y - drag.start.y) / drag.card.canvasHeight)
                model?.placeSelected(scale: drag.card.scale + delta)
            } else { model?.placeSelected(x: drag.card.x + p.x - drag.start.x, y: drag.card.y + p.y - drag.start.y) }
        } else { forward(event, id: pointerID, type: 0, button: 1) }
    }
    override func mouseUp(with event: NSEvent) {
        if let model, let draft = model.annotationDraft, !model.editingText {
            model.annotationDraft = nil; annotationDrag = nil
            if draft.bounds.width > 2 || draft.bounds.height > 2 { model.commitAnnotation(draft) }
            needsDisplay = true; return
        }
        annotationDrag = nil
        if drag != nil { drag = nil }
        else { forward(event, id: pointerID, type: 2, button: 1) }
        pointerID = nil
    }
    override func mouseMoved(with event: NSEvent) {
        if model?.annotationToolbar == true && model?.annotationTool == .eraser {
            eraseTarget = annotationAt(point(event))?.id; needsDisplay = true; return
        }
        let id = hit(point(event))?.id
        if hoveredID != id, let old = hoveredID { model?.renderers[old]?.mouse(0, x: -1, y: -1, button: 0) }
        hoveredID = id; forward(event, id: id, type: 0, button: 0)
    }
    override func mouseExited(with event: NSEvent) {
        eraseTarget = nil; needsDisplay = true
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


extension CardPreviewView: NSTextViewDelegate {
    func textDidChange(_ notification: Notification) {
        guard let editor = textEditor, var draft = model?.annotationDraft else { return }
        var text = editor.string
        while text.utf16.count > 16384 { text.removeLast() }
        if text != editor.string { editor.string = text }
        draft.text = text; model?.annotationDraft = draft
    }
}

@MainActor
private final class AnnotationTextView: NSTextView {
    weak var owner: CardPreviewView?
    override func keyDown(with event: NSEvent) {
        if event.keyCode == 53 { owner?.endText(cancel: true); return }
        if [36, 76].contains(event.keyCode) && !event.modifierFlags.intersection([.command, .control]).isEmpty {
            owner?.endText(cancel: false); return
        }
        super.keyDown(with: event)
    }
    override func resignFirstResponder() -> Bool {
        owner?.model?.finishText()
        return super.resignFirstResponder()
    }
}
