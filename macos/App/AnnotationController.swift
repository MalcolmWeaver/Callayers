import AppKit
import Combine

enum AnnotationGeometry {
    static func path(_ item: Annotation) -> CGPath {
        let p = CGMutablePath(), b = item.box
        let rect = CGRect(x:b.x,y:b.y,width:b.width,height:b.height)
        switch item.tool {
        case .rectangle: p.addRect(rect)
        case .ellipse: p.addEllipse(in:rect)
        case .pen, .line, .arrow:
            let points = item.points.isEmpty ? [InkPoint(x:b.x,y:b.y),InkPoint(x:b.x+b.width,y:b.y+b.height)] : item.points
            if let first = points.first { p.move(to:CGPoint(x:first.x,y:first.y)); for point in points.dropFirst() { p.addLine(to:CGPoint(x:point.x,y:point.y)) } }
            if item.tool == .arrow, points.count >= 2 {
                let a = points[points.count-2], z = points.last!, angle = atan2(z.y-a.y,z.x-a.x), length = max(16,item.size*4)
                for offset in [-0.5,0.5] { p.move(to:CGPoint(x:z.x,y:z.y)); p.addLine(to:CGPoint(x:z.x-length*cos(angle+offset),y:z.y-length*sin(angle+offset))) }
            }
        default: break
        }
        return p
    }
    static func hit(_ item: Annotation, point: InkPoint, tolerance: Double) -> Bool {
        let b = item.box, p = CGPoint(x:point.x,y:point.y)
        if item.tool == .text { return CGRect(x:b.x,y:b.y,width:b.width,height:b.height).insetBy(dx:-tolerance,dy:-tolerance).contains(p) }
        return path(item).copy(strokingWithWidth:item.size+2*tolerance,lineCap:.round,lineJoin:.round,miterLimit:10).contains(p)
    }
    static func move(_ item: Annotation, dx: Double, dy: Double) -> Annotation {
        var result = item
        let x = min(max(dx,-item.box.x),1280-item.box.x-item.box.width)
        let y = min(max(dy,-item.box.y),720-item.box.y-item.box.height)
        result.box.x += x; result.box.y += y
        result.points = item.points.map { InkPoint(x:$0.x+x,y:$0.y+y) }; return result
    }
    static func resize(_ item: Annotation, width: Double, height: Double) -> Annotation {
        var result = item
        result.box.width = min(1280-item.box.x,max(8,width)); result.box.height = min(720-item.box.y,max(8,height))
        let sx = result.box.width / max(1,item.box.width), sy = result.box.height / max(1,item.box.height)
        result.points = item.points.map { InkPoint(x:item.box.x+($0.x-item.box.x)*sx,y:item.box.y+($0.y-item.box.y)*sy) }
        if item.tool == .text { result.font = min(160,max(8,item.font*min(sx,sy))) }; return result
    }
}

@MainActor final class AnnotationController: ObservableObject {
    @Published var tool: AnnotationTool = .select
    @Published var color = "#ffd166" { didSet { updateSelectedStyle() } }
    @Published var size = 6.0 { didSet { updateSelectedStyle() } }
    @Published var font = 36.0 { didSet { updateSelectedStyle() } }
    @Published var selectedID: UUID?
    @Published var editing: Annotation?
    @Published private(set) var items: [Annotation] = []
    var changed: (([Annotation],Bool)->Void)?
    var selected: Annotation? { items.first { $0.id == selectedID } }
    func load(_ items: [Annotation]) { self.items = items; selectedID = nil; editing = nil }
    func put(_ item: Annotation, creation: Bool = false) {
        guard item.valid, items.count < 300 || items.contains(where:{$0.id == item.id}) else { return }
        var candidate = items
        if let i = candidate.firstIndex(where:{$0.id == item.id}) { candidate[i] = item } else { candidate.append(item) }
        guard candidate.reduce(0,{$0+$1.points.count}) <= 65536 else { return }
        items = candidate; selectedID = item.id; changed?(items,creation)
    }
    func remove(_ id: UUID) { items.removeAll {$0.id == id}; if selectedID == id { selectedID = nil }; changed?(items,false) }
    func clear() { items = []; selectedID = nil; editing = nil; changed?(items,false) }
    func finishText(_ text: String?) {
        guard var item = editing else { return }; editing = nil
        guard let text, !text.trimmingCharacters(in:.whitespacesAndNewlines).isEmpty else { return }
        item.text = String(text.prefix(4096))
        put(item,creation:!items.contains(where:{$0.id == item.id}))
    }
    private func updateSelectedStyle() {
        guard var item = selected, editing == nil else { return }
        item.color = color; item.size = size; item.font = font; put(item)
    }
}
