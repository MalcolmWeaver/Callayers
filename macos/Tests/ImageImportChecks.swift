import Foundation
import ImageIO
import CoreGraphics
import UniformTypeIdentifiers

@main
struct ImageImportChecks {
    static func require(_ value: @autoclosure () -> Bool, _ message: String) throws {
        if !value() { throw WorkspaceError.invalid(message) }
    }
    static func rejects(_ body: () throws -> Void) throws {
        do { try body() } catch { return }
        throw WorkspaceError.invalid("Invalid image was accepted")
    }
    static func main() throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: directory) }
        let context = CGContext(data: nil, width: 320, height: 160, bitsPerComponent: 8, bytesPerRow: 1280,
            space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        context.setFillColor(CGColor(red: 1, green: 0, blue: 0, alpha: 1))
        context.fill(CGRect(x: 0, y: 0, width: 160, height: 160))
        let raw = NSMutableData()
        let destination = CGImageDestinationCreateWithData(raw, UTType.png.identifier as CFString, 1, nil)!
        CGImageDestinationAddImage(destination, context.makeImage()!, nil)
        try require(CGImageDestinationFinalize(destination), "Fixture could not encode")
        let imported = try ImageImport.decode(raw as Data, name: "photo.png")
        try require(imported.width == 320 && imported.height == 160, "Image proportions changed")
        let store = WorkspaceStore(directory: directory)
        var document = try store.load()
        let id = UUID()
        try store.saveImage(imported.png, id: id)
        var card = WorkspaceCard(name: imported.name, html: ImportedImage.html)
        card.imageID = id; card.contentWidth = 320; card.contentHeight = 160
        card.place(x: 1200, y: 700, scale: 2)
        try require(card.x == 640 && card.y == 400, "Custom canvas placement escaped the scene")
        document.cards = [card]; document.selectedID = card.id
        try store.save(document)
        let reopened = try WorkspaceStore(directory: directory).load()
        try require(reopened.cards[0] == card, "Image metadata did not survive reopening")
        let html = try store.renderedHTML(for: reopened.cards[0])
        try require(html.contains("data:image/png;base64,"), "Local asset did not resolve")
        try require(!card.html.contains("base64") && !card.html.contains(directory.path), "Stored card leaks local image data or paths")
        if CommandLine.arguments.count > 1 {
            try html.write(toFile: CommandLine.arguments[1], atomically: true, encoding: .utf8)
        }
        try rejects { _ = try ImageImport.decode(Data("not an image".utf8), name: "fake.png") }
        try rejects { try store.saveImage(imported.png, id: id) }
        try FileManager.default.removeItem(at: directory.appendingPathComponent("images/\(id.uuidString).png"))
        try rejects { _ = try store.renderedHTML(for: card) }
        print("PASS image decode, aspect ratio, local asset persistence, missing/corrupt asset rejection and custom geometry")

        let cleanup = WorkspaceStore(directory: directory.appendingPathComponent("cleanup"))
        var cleanupDocument = try cleanup.load()
        let retained = UUID(), orphan = UUID()
        try cleanup.saveImage(imported.png, id: retained); try cleanup.saveImage(imported.png, id: orphan)
        var withBackup = WorkspaceCard(name: "Revised image", html: "<p>Replacement</p>")
        withBackup.previousVersion = CardVersion(name: "Original image", html: ImportedImage.html, width: 320, height: 160, imageID: retained)
        cleanupDocument.cards = [withBackup]; try cleanup.save(cleanupDocument)
        let images = cleanup.directory.appendingPathComponent("images")
        try require(FileManager.default.fileExists(atPath: images.appendingPathComponent(retained.uuidString + ".png").path), "Cleanup removed the previous card version's image")
        try require(!FileManager.default.fileExists(atPath: images.appendingPathComponent(orphan.uuidString + ".png").path), "Cleanup retained an orphan image")
        cleanupDocument.cards = []; try cleanup.save(cleanupDocument)
        try require(FileManager.default.fileExists(atPath: images.appendingPathComponent(retained.uuidString + ".png").path), "Cleanup broke the workspace backup")
        cleanupDocument.promptDraft = "changed"; try cleanup.save(cleanupDocument)
        try require(!FileManager.default.fileExists(atPath: images.appendingPathComponent(retained.uuidString + ".png").path), "Cleanup retained an image after its last saved reference was removed")
        print("PASS unused image cleanup preserves card versions and the recoverable workspace backup")

        let jpeg = NSMutableData()
        let jpegDestination = CGImageDestinationCreateWithData(jpeg, UTType.jpeg.identifier as CFString, 1, nil)!
        CGImageDestinationAddImage(jpegDestination, context.makeImage()!, [kCGImagePropertyOrientation: 6] as CFDictionary)
        try require(CGImageDestinationFinalize(jpegDestination), "JPEG fixture failed")
        let rotated = try ImageImport.decode(jpeg as Data, name: "rotated.jpg")
        try require(rotated.width == 160 && rotated.height == 320, "EXIF orientation was not applied")
        print("PASS JPEG orientation is normalized before local storage")

        let legacy = """
        {"version":1,"cards":[{"id":"00000000-0000-0000-0000-000000000001","name":"Old card","html":"hi","state":"{}","visible":true,"x":160,"y":120,"scale":1,"edited":0}]}
        """
        let migrated = try JSONDecoder().decode(WorkspaceDocument.self, from: Data(legacy.utf8)).validated()
        try require(migrated.version == 3 && migrated.cards[0].width == 640 && migrated.cards[0].height == 400, "Legacy workspace dimensions changed")
        print("PASS existing workspaces retain their original card dimensions")
    }
}
