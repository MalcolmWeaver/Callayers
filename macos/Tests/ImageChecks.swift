import Foundation
import AppKit
@main struct ImageChecks {
    static func main() throws {
        func check(_ b: Bool, _ m: String) throws { if !b { throw WorkspaceError.invalid(m) } }
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let url = root.appendingPathComponent("picture.png")
        let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 1600, pixelsHigh: 900, bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
        try bitmap.representation(using: .png, properties: [:])!.write(to: url)
        let card = try ImageImport.prepare(url: url)
        try check(card.width == 1280 && card.height == 720 && card.image != nil, "Image limits or local storage wrong")
        try check(card.html.contains("callayers-image:local") && !card.html.contains("data:"), "Saved HTML exposed image bytes")
        try Data("invalid".utf8).write(to: url)
        do { _ = try ImageImport.prepare(url: url); throw NSError(domain: "Bad image accepted", code: 1) }
        catch WorkspaceError.invalid { }
        print("PASS imported image dimensions, local references and corrupt input rejection")
    }
}
