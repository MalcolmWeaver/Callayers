import AppKit
import CoreMedia

@main
struct AnnotationChecks {
    static func require(_ value: @autoclosure () -> Bool, _ message: String) throws {
        if !value() { throw WorkspaceError.invalid(message) }
    }
    static func main() throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: directory) }
        let stroke = WorkspaceAnnotation(kind: .pen, points: [AnnotationPoint(CGPoint(x: 40, y: 60)), AnnotationPoint(CGPoint(x: 200, y: 60))], color: 0xff0000, thickness: 6)
        try require(stroke.contains(CGPoint(x: 100, y: 64)), "Stroke hit testing missed the pointer")
        try require(!stroke.contains(CGPoint(x: 100, y: 90)), "Eraser would remove a distant stroke")
        let moved = stroke.moved(dx: 5000, dy: -100)
        try require(moved.valid && moved.bounds.maxX == 1280 && moved.bounds.minY == 0, "Movement escaped the scene")
        let resized = stroke.resized(factor: 3)
        try require(resized.valid && resized.bounds.width == 480, "Stroke resizing lost its geometry")
        var text = WorkspaceAnnotation(kind: .text, points: [AnnotationPoint(CGPoint(x: 300, y: 200)), AnnotationPoint(CGPoint(x: 620, y: 340))])
        text.text = "Committed\nSecond line"
        let store = WorkspaceStore(directory: directory)
        var document = try store.load(); document.annotationItems = [stroke, text]
        try store.save(document)
        let restored = try WorkspaceStore(directory: directory).load()
        try require(restored == document, "Annotations did not survive reopening")
        document.annotationItems.removeAll { $0.id == stroke.id }; try store.save(document)
        let afterErase = try store.load()
        try require(afterErase.annotationItems == [text], "Erasing changed the wrong annotation")
        document.annotationItems = []; try store.save(document)
        let afterClear = try store.load()
        try require(afterClear.annotationItems.isEmpty, "Clear was not saved")
        print("PASS annotation hit testing, bounded movement/resizing, erase/clear and persistence")

        let image = AnnotationRenderer.image([stroke, text])!
        let data = image.dataProvider!.data! as Data
        let offset = 90 * image.bytesPerRow + 150 * 4
        try require(data[offset + 2] > 240 && data[offset + 3] > 240, "Annotation was not drawn at its top-left logical coordinates")
        try require(data[3] == 0, "Annotation layer background was not transparent")
        let textImage = AnnotationRenderer.image([text])!
        let textBytes = textImage.dataProvider!.data! as Data
        var glyphs = 0
        for y in 0..<1080 {
            for x in 0..<1920 where textBytes[y * textImage.bytesPerRow + x * 4 + 3] > 0 {
                glyphs += 1
                try require(x >= 450 && x < 930 && y >= 300 && y < 510, "Text escaped its editing rectangle")
            }
        }
        try require(glyphs > 100, "Multiline text did not render")
        let original = text
        text.text = "Private draft"; text.fontSize = 40
        try require(AnnotationRenderer.image([text])!.dataProvider!.data! as Data != textBytes, "Draft text was indistinguishable from committed content")
        try require(original.text == "Committed\nSecond line", "Editing a draft mutated the committed annotation")
        print("PASS transparent annotation pixels, multiline text layout and independent draft content")
    }
}
