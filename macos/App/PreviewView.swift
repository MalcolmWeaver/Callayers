import SwiftUI
import AppKit

@MainActor
struct PreviewView: NSViewRepresentable {
    @ObservedObject var model: PreviewModel

    func makeNSView(context: Context) -> CardPreviewView {
        let view = CardPreviewView(frame: .zero)
        view.renderer = model.renderer
        return view
    }

    func updateNSView(_ view: CardPreviewView, context: Context) {
        view.image = model.image
        view.checkerboard = model.checkerboard
        view.needsDisplay = true
    }
}

final class CardPreviewView: NSView {
    var renderer: CLRendererBridge?
    var image: NSImage?
    var checkerboard = true
    private var mouseTracking: NSTrackingArea?

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func updateTrackingAreas() {
        if let area = mouseTracking { removeTrackingArea(area) }
        let area = NSTrackingArea(rect: .zero,
            options: [.mouseMoved, .activeInKeyWindow, .inVisibleRect], owner: self, userInfo: nil)
        addTrackingArea(area)
        mouseTracking = area
        super.updateTrackingAreas()
    }

    override func draw(_ dirtyRect: NSRect) {
        NSColor(calibratedWhite: 0.16, alpha: 1).setFill()
        bounds.fill()
        if checkerboard {
            let tile: CGFloat = 24
            NSColor(calibratedWhite: 0.23, alpha: 1).setFill()
            for y in stride(from: CGFloat(0), to: bounds.height, by: tile) {
                for x in stride(from: CGFloat(0), to: bounds.width, by: tile) {
                    if (Int(x / tile) + Int(y / tile)) % 2 == 0 {
                        NSRect(x: x, y: y, width: tile, height: tile).fill()
                    }
                }
            }
        }
        image?.draw(in: bounds, from: .zero, operation: .sourceOver, fraction: 1, respectFlipped: true, hints: nil)
    }

    private func position(_ event: NSEvent) -> (Int, Int) {
        let point = convert(event.locationInWindow, from: nil)
        return (Int(point.x / max(1, bounds.width) * 1280), Int(point.y / max(1, bounds.height) * 720))
    }

    private func forward(_ event: NSEvent, type: Int, button: Int) {
        let (x, y) = position(event)
        renderer?.mouse(type, x: x, y: y, button: button)
    }

    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        forward(event, type: 1, button: 1)
    }
    override func mouseUp(with event: NSEvent) { forward(event, type: 2, button: 1) }
    override func mouseMoved(with event: NSEvent) { forward(event, type: 0, button: 0) }
    override func mouseDragged(with event: NSEvent) { forward(event, type: 0, button: 1) }
    override func rightMouseDown(with event: NSEvent) { forward(event, type: 1, button: 3) }
    override func rightMouseUp(with event: NSEvent) { forward(event, type: 2, button: 3) }
    override func scrollWheel(with event: NSEvent) {
        let (x, y) = position(event)
        let factor = event.hasPreciseScrollingDeltas ? 1.0 : 24.0
        renderer?.scroll(x: x, y: y, deltaX: Int(event.scrollingDeltaX * factor), deltaY: Int(event.scrollingDeltaY * factor))
    }
    override func keyDown(with event: NSEvent) { renderer?.keyEvent(event) }
    override func keyUp(with event: NSEvent) { renderer?.keyEvent(event) }
    override func resignFirstResponder() -> Bool {
        renderer?.blur()
        return super.resignFirstResponder()
    }
}
