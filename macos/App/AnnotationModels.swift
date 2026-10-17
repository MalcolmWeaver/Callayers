import Foundation

enum AnnotationTool: String, Codable, CaseIterable { case select, pen, rectangle, ellipse, line, arrow, text, eraser }
struct InkPoint: Codable, Equatable { var x: Double; var y: Double }
struct InkBox: Codable, Equatable { var x: Double; var y: Double; var width: Double; var height: Double }
struct Annotation: Codable, Identifiable, Equatable {
    var id = UUID()
    var tool: AnnotationTool
    var color = "#ffd166"
    var size = 6.0
    var font = 36.0
    var box: InkBox
    var points: [InkPoint] = []
    var text = ""
    var valid: Bool {
        ![.select, .eraser].contains(tool) && color.range(of: "^#[0-9a-fA-F]{6}$", options: .regularExpression) != nil &&
        size.isFinite && (1...30).contains(size) && font.isFinite && (8...160).contains(font) &&
        [box.x,box.y,box.width,box.height].allSatisfy(\.isFinite) && box.x >= 0 && box.y >= 0 &&
        box.width >= 0 && box.height >= 0 && box.x + box.width <= 1280.001 && box.y + box.height <= 720.001 &&
        text.utf16.count <= 4096 && points.count <= 65536 && points.allSatisfy {
            $0.x.isFinite && $0.y.isFinite && (0...1280).contains($0.x) && (0...720).contains($0.y)
        }
    }
}
