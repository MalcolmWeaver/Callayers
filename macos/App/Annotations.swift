import Foundation
import CoreGraphics

struct AnnotationPoint: Codable, Equatable {
    var x: Double
    var y: Double
    init(_ point: CGPoint) { x = min(1280, max(0, point.x)); y = min(720, max(0, point.y)) }
    var point: CGPoint { CGPoint(x: x, y: y) }
}

enum AnnotationTool: String, CaseIterable, Codable {
    case select, pen, line, arrow, rectangle, ellipse, text, eraser
}

struct WorkspaceAnnotation: Codable, Identifiable, Equatable {
    var id = UUID()
    var kind: AnnotationTool
    var points: [AnnotationPoint]
    var color: UInt32 = 0x56ddca
    var thickness = 4.0
    var text = ""
    var fontSize = 28.0

    var bounds: CGRect {
        let xs = points.map(\.x), ys = points.map(\.y)
        let x = xs.min() ?? 0, y = ys.min() ?? 0
        return CGRect(x: x, y: y, width: max(1, (xs.max() ?? x) - x), height: max(1, (ys.max() ?? y) - y))
    }
    var valid: Bool {
        guard ![.select, .eraser].contains(kind), points.count >= 2, points.count <= 8192,
              color <= 0xffffff, thickness.isFinite, (1...32).contains(thickness),
              fontSize.isFinite, (8...160).contains(fontSize), text.utf16.count <= 16384,
              points.allSatisfy({ $0.x.isFinite && $0.y.isFinite && (0...1280).contains($0.x) && (0...720).contains($0.y) }) else { return false }
        return kind == .pen || points.count == 2
    }
    var path: CGPath {
        let path = CGMutablePath()
        guard let first = points.first, let last = points.last else { return path }
        switch kind {
        case .rectangle: path.addRect(bounds)
        case .ellipse: path.addEllipse(in: bounds)
        case .pen, .line, .arrow:
            path.move(to: first.point)
            for p in points.dropFirst() { path.addLine(to: p.point) }
            if kind == .arrow {
                let angle = atan2(last.y - first.y, last.x - first.x), length = max(14, thickness * 4)
                for offset in [-0.5, 0.5] {
                    path.move(to: last.point)
                    path.addLine(to: CGPoint(x: last.x - cos(angle + offset) * length, y: last.y - sin(angle + offset) * length))
                }
            }
        default: break
        }
        return path
    }
    func contains(_ point: CGPoint) -> Bool {
        if kind == .text { return bounds.contains(point) }
        return path.copy(strokingWithWidth: max(12, thickness + 8), lineCap: .round, lineJoin: .round, miterLimit: 10).contains(point)
    }
    func moved(dx: Double, dy: Double) -> WorkspaceAnnotation {
        var copy = self
        let r = bounds
        let x = min(1280 - r.maxX, max(-r.minX, dx)), y = min(720 - r.maxY, max(-r.minY, dy))
        copy.points = points.map { AnnotationPoint(CGPoint(x: $0.x + x, y: $0.y + y)) }
        return copy
    }
    func resized(factor: Double) -> WorkspaceAnnotation {
        guard factor.isFinite else { return self }
        var copy = self
        let r = bounds
        var scale = min(min((1280 - r.minX) / r.width, (720 - r.minY) / r.height), max(0.1, factor))
        if kind == .text { scale = min(160 / fontSize, max(8 / fontSize, scale)) }
        copy.points = points.map { AnnotationPoint(CGPoint(x: r.minX + ($0.x - r.minX) * scale, y: r.minY + ($0.y - r.minY) * scale)) }
        if kind == .text { copy.fontSize *= scale }
        return copy
    }
}
