import SwiftUI
import AppKit

@MainActor final class WorkspaceCloseDelegate: NSObject, NSWindowDelegate {
    nonisolated(unsafe) weak var original: NSWindowDelegate?
    init(original: NSWindowDelegate?) { self.original = original }
    func windowShouldClose(_ sender: NSWindow) -> Bool { NSApp.terminate(nil); return false }
    override func responds(to selector: Selector!) -> Bool { super.responds(to:selector) || (original?.responds(to:selector) ?? false) }
    override func forwardingTarget(for selector: Selector!) -> Any? { original }
}
struct PreviewView: NSViewRepresentable {
    @ObservedObject var model: WorkspaceModel
    @ObservedObject var annotations: AnnotationController
    init(model: WorkspaceModel) { self.model = model; self.annotations = model.annotations }
    func makeNSView(context: Context) -> WorkspaceCanvas { let view = WorkspaceCanvas(); view.model = model; return view }
    func updateNSView(_ view: WorkspaceCanvas,context: Context) { view.model = model; view.syncEditor(); view.needsDisplay = true }
}
@MainActor final class WorkspaceCanvas: NSView, NSTextInputClient {
    weak var model: WorkspaceModel?
    private var closeDelegate: WorkspaceCloseDelegate?
    private var tracking: NSTrackingArea?
    private var focusedID: UUID?
    private var pointerID: UUID?
    private var hoveredID: UUID?
    private var cardDrag: (card:WorkspaceCard,start:InkPoint,resize:Bool)?
    private var annotationDrag: (item:Annotation,start:InkPoint,resize:Bool)?
    private var draft: Annotation?
    private var start: InkPoint?
    private var eraserID: UUID?
    private var editor: AnnotationTextEditor?
    private var editorID: UUID?
    private var marked = NSAttributedString(string:"")
    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }
    private var sx: CGFloat { bounds.width/1280 }
    private var sy: CGFloat { bounds.height/720 }
    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        guard let window,window.delegate !== closeDelegate else { return }
        let delegate = WorkspaceCloseDelegate(original:window.delegate); closeDelegate = delegate; window.delegate = delegate
    }
    override func updateTrackingAreas() {
        if let tracking { removeTrackingArea(tracking) }
        let area = NSTrackingArea(rect:.zero,options:[.mouseMoved,.mouseEnteredAndExited,.activeInKeyWindow,.inVisibleRect],owner:self,userInfo:nil)
        addTrackingArea(area); tracking = area; super.updateTrackingAreas()
    }
    private func rect(_ card: WorkspaceCard) -> CGRect { CGRect(x:card.x*sx,y:card.y*sy,width:card.width*sx,height:card.height*sy) }
    private func rect(_ item: Annotation) -> CGRect { CGRect(x:item.box.x*sx,y:item.box.y*sy,width:item.box.width*sx,height:item.box.height*sy) }
    private func handle(_ r: CGRect) -> CGRect { CGRect(x:r.maxX-5,y:r.maxY-5,width:10,height:10) }
    override func draw(_ dirtyRect: NSRect) {
        NSColor(calibratedWhite:0.07,alpha:1).setFill(); bounds.fill()
        guard let model else { return }
        if let image = model.video.preview { image.draw(in:bounds,from:.zero,operation:.copy,fraction:1,respectFlipped:true,hints:nil) }
        if let context = NSGraphicsContext.current?.cgContext {
            context.saveGState(); context.scaleBy(x:sx,y:sy)
            if let draft { AnnotationRenderer.draw([draft],in:context) }
            context.restoreGState()
        }
        if let card = model.selected,card.visible,model.annotations.tool == .select,model.annotations.selectedID == nil {
            selection(rect(card))
        }
        if let item = model.annotations.selected,model.annotations.editing?.id != item.id { selection(rect(item)) }
        if marked.length > 0, let card = model.selected {
            let r = rect(card)
            marked.draw(at:NSPoint(x:r.minX,y:r.maxY+8))
        }
        if let item = model.annotations.items.first(where:{$0.id == eraserID}) {
            NSColor.systemRed.setStroke(); let path = NSBezierPath(rect:rect(item).insetBy(dx:-4,dy:-4)); path.lineWidth = 2; path.stroke()
        }
    }
    private func selection(_ r: CGRect) {
        NSColor.systemCyan.setStroke(); let outline = NSBezierPath(rect:r.insetBy(dx:-3,dy:-3)); outline.lineWidth = 2; outline.stroke()
        NSColor.systemCyan.setFill(); handle(r).fill()
    }
    private func point(_ event: NSEvent) -> InkPoint {
        let p = convert(event.locationInWindow,from:nil)
        return InkPoint(x:min(1280,max(0,p.x/max(sx,0.001))),y:min(720,max(0,p.y/max(sy,0.001))))
    }
    private func hit(_ p: InkPoint) -> WorkspaceCard? { model?.visibleCards.reversed().first { p.x >= $0.x && p.x < $0.x+$0.width && p.y >= $0.y && p.y < $0.y+$0.height } }
    private func annotationHit(_ p: InkPoint) -> Annotation? { model?.annotations.items.reversed().first { AnnotationGeometry.hit($0,point:p,tolerance:5/max(sx,0.01)) } }
    private func forward(_ event: NSEvent,id: UUID?,type: Int,button: Int) {
        guard let model,let card = model.visibleCards.first(where:{$0.id == id}) else { return }
        let p = point(event); model.renderers[card.id]?.mouse(type,x:Int((p.x-card.x)/card.scale),y:Int((p.y-card.y)/card.scale),button:button)
    }
    override func mouseDown(with event: NSEvent) {
        guard let model,model.auth.allowed,model.video.cameraReady,!model.locked,!model.closing else { return }
        window?.makeFirstResponder(self)
        let p = point(event), local = convert(event.locationInWindow,from:nil), controller = model.annotations
        if controller.tool == .eraser { if let item = annotationHit(p) { controller.remove(item.id) }; needsDisplay = true; return }
        if controller.tool == .text {
            let x = min(p.x,1240),y = min(p.y,680)
            let b = InkBox(x:x,y:y,width:min(420,1280-x),height:min(180,720-y))
            controller.editing = Annotation(tool:.text,color:controller.color,size:controller.size,font:controller.font,box:b)
            syncEditor(); return
        }
        if controller.tool != .select {
            start = p; controller.selectedID = nil; model.select(nil)
            draft = Annotation(tool:controller.tool,color:controller.color,size:controller.size,font:controller.font,box:InkBox(x:p.x,y:p.y,width:0,height:0),points:[p]); return
        }
        if let item = controller.selected,handle(rect(item)).insetBy(dx:-4,dy:-4).contains(local) {
            annotationDrag = (item,p,true); return
        }
        if let item = annotationHit(p) {
            controller.selectedID = item.id; model.select(nil)
            if item.tool == .text,event.clickCount == 2 { controller.editing = item; syncEditor() }
            else { annotationDrag = (item,p,false) }; return
        }
        controller.selectedID = nil
        if let card = model.selected,card.visible {
            let r = rect(card),resize = handle(r).insetBy(dx:-4,dy:-4).contains(local)
            if resize || (r.insetBy(dx:-7,dy:-7).contains(local) && !r.insetBy(dx:5,dy:5).contains(local)) {
                model.renderers[card.id]?.blur(); focusedID = nil; cardDrag = (card,p,resize); return
            }
        }
        let card = hit(p); model.select(card?.id); focusedID = card?.id; pointerID = card?.id
        forward(event,id:pointerID,type:1,button:1)
    }
    override func mouseDragged(with event: NSEvent) {
        guard let model else { return }; let p = point(event)
        if let drag = cardDrag {
            if drag.resize {
                let delta = max((p.x-drag.start.x)/drag.card.contentWidth,(p.y-drag.start.y)/drag.card.contentHeight)
                model.placeSelected(scale:drag.card.scale+delta)
            } else { model.placeSelected(x:drag.card.x+p.x-drag.start.x,y:drag.card.y+p.y-drag.start.y) }
        } else if let drag = annotationDrag {
            let item = drag.resize ? AnnotationGeometry.resize(drag.item,width:drag.item.box.width+p.x-drag.start.x,height:drag.item.box.height+p.y-drag.start.y) : AnnotationGeometry.move(drag.item,dx:p.x-drag.start.x,dy:p.y-drag.start.y)
            model.annotations.put(item)
        } else if var item = draft,let start {
            if item.tool == .pen { if item.points.count < 8192 { item.points.append(p) } }
            else { item.points = [start,p] }
            let xs = item.points.map(\.x),ys = item.points.map(\.y)
            item.box = InkBox(x:xs.min()!,y:ys.min()!,width:xs.max()!-xs.min()!,height:ys.max()!-ys.min()!)
            draft = item; needsDisplay = true
        } else { forward(event,id:pointerID,type:0,button:1) }
    }
    override func mouseUp(with event: NSEvent) {
        if let draft { if draft.box.width+draft.box.height > 2 { model?.annotations.put(draft,creation:true) }; self.draft = nil; start = nil; needsDisplay = true }
        else if cardDrag == nil && annotationDrag == nil { forward(event,id:pointerID,type:2,button:1) }
        cardDrag = nil; annotationDrag = nil; pointerID = nil
    }
    override func mouseMoved(with event: NSEvent) {
        guard let model else { return }
        if model.annotations.tool == .eraser { eraserID = annotationHit(point(event))?.id; needsDisplay = true; return }
        eraserID = nil
        let id = hit(point(event))?.id
        if hoveredID != id,let old = hoveredID { model.renderers[old]?.mouse(0,x:-1,y:-1,button:0) }
        hoveredID = id; forward(event,id:id,type:0,button:0)
    }
    override func mouseExited(with event: NSEvent) { if let id = hoveredID { model?.renderers[id]?.mouse(0,x:-1,y:-1,button:0) }; hoveredID = nil; eraserID = nil; needsDisplay = true }
    override func scrollWheel(with event: NSEvent) {
        guard let card = hit(point(event)),model?.annotations.tool == .select else { return }
        let p = point(event),factor = event.hasPreciseScrollingDeltas ? 1.0 : 24.0
        model?.renderers[card.id]?.scroll(x:Int((p.x-card.x)/card.scale),y:Int((p.y-card.y)/card.scale),deltaX:Int(event.scrollingDeltaX*factor),deltaY:Int(event.scrollingDeltaY*factor))
    }
    override func keyDown(with event: NSEvent) {
        if let id = model?.annotations.selectedID,[51,117].contains(event.keyCode) { model?.annotations.remove(id); return }
        guard let id = focusedID,model?.document.selectedID == id,model?.closing == false else { return }; model?.renderers[id]?.rawKeyEvent(event); interpretKeyEvents([event])
    }
    override func keyUp(with event: NSEvent) { guard let id = focusedID,model?.document.selectedID == id else { return }; model?.renderers[id]?.keyEvent(event) }
    override func resignFirstResponder() -> Bool { model?.renderers.values.forEach {$0.blur()}; focusedID = nil; marked = NSAttributedString(string:""); return super.resignFirstResponder() }
    func insertText(_ string: Any,replacementRange: NSRange) {
        let text = (string as? NSAttributedString)?.string ?? (string as? String ?? "")
        marked = NSAttributedString(string:"")
        if let id = focusedID,model?.document.selectedID == id { model?.renderers[id]?.insertText(text) }
        needsDisplay = true
    }
    func setMarkedText(_ string: Any,selectedRange: NSRange,replacementRange: NSRange) {
        let text = (string as? NSAttributedString)?.string ?? (string as? String ?? "")
        marked = NSAttributedString(string:text,attributes:[.font:NSFont.systemFont(ofSize:16),.foregroundColor:NSColor.white,.backgroundColor:NSColor.darkGray])
        needsDisplay = true
    }
    func unmarkText() { marked = NSAttributedString(string:""); needsDisplay = true }
    func selectedRange() -> NSRange { NSRange(location:0,length:0) }
    func markedRange() -> NSRange { NSRange(location:marked.length == 0 ? NSNotFound : 0,length:marked.length) }
    func hasMarkedText() -> Bool { marked.length > 0 }
    func validAttributesForMarkedText() -> [NSAttributedString.Key] { [] }
    func attributedSubstring(forProposedRange range: NSRange,actualRange: NSRangePointer?) -> NSAttributedString? { nil }
    func firstRect(forCharacterRange range: NSRange,actualRange: NSRangePointer?) -> NSRect {
        let r = model?.selected.map { rect($0) } ?? bounds
        return window?.convertToScreen(convert(NSRect(x:r.minX,y:r.minY,width:1,height:20),to:nil)) ?? .zero
    }
    func characterIndex(for point: NSPoint) -> Int { 0 }
    override func doCommand(by selector: Selector) {
        if selector == #selector(insertNewline(_:)) { insertText("\n",replacementRange:NSRange(location:NSNotFound,length:0)) }
        if selector == #selector(insertTab(_:)) { insertText("\t",replacementRange:NSRange(location:NSNotFound,length:0)) }
        if NSStringFromSelector(selector) == "paste:",let text = NSPasteboard.general.string(forType:.string) {
            insertText(String(text.prefix(16384)),replacementRange:NSRange(location:NSNotFound,length:0))
        }
    }
    func syncEditor() {
        guard let controller = model?.annotations,let item = controller.editing else {
            editor?.removeFromSuperview(); editor = nil; editorID = nil; return
        }
        if editorID != item.id {
            editor?.removeFromSuperview()
            let input = AnnotationTextEditor(frame:rect(item)); input.isRichText = false; input.drawsBackground = false
            input.textContainerInset = .zero; input.textContainer?.lineFragmentPadding = 0
            input.isHorizontallyResizable = false; input.isVerticallyResizable = false
            input.string = item.text; input.typingAttributes = AnnotationRenderer.textAttributes(item,scale:sx)
            input.font = NSFont.systemFont(ofSize:item.font*sx); input.textColor = AnnotationRenderer.color(item.color)
            input.textContainer?.containerSize = CGSize(width:item.box.width*sx,height:item.box.height*sy)
            input.finished = { [weak self] value in self?.model?.annotations.finishText(value) }
            input.wantsLayer = true; input.layer?.borderColor = NSColor.systemCyan.cgColor; input.layer?.borderWidth = 1
            addSubview(input); editor = input; editorID = item.id; window?.makeFirstResponder(input)
        }
        editor?.frame = rect(item)
    }
}
