import Foundation
import ImageIO
import UniformTypeIdentifiers
import CoreGraphics

enum ImageImport {
    static func prepare(url: URL) throws -> CardContent {
        guard ["png","jpg","jpeg","bmp"].contains(url.pathExtension.lowercased()) else {
            throw WorkspaceError.invalid("Choose a PNG, JPEG or BMP image.")
        }
        let attributes = try FileManager.default.attributesOfItem(atPath: url.path)
        guard let size = attributes[.size] as? NSNumber, size.intValue <= 32 * 1024 * 1024 else {
            throw WorkspaceError.invalid("Choose an image smaller than 32 MB.")
        }
        let data = try Data(contentsOf: url, options: .mappedIfSafe)
        guard let source = CGImageSourceCreateWithData(data as CFData, nil),
              let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              let w = properties[kCGImagePropertyPixelWidth] as? NSNumber,
              let h = properties[kCGImagePropertyPixelHeight] as? NSNumber,
              w.intValue > 0, h.intValue > 0, w.doubleValue * h.doubleValue <= 24000000 else {
            throw WorkspaceError.invalid("Choose a readable image with at most 24 million pixels.")
        }
        var maxPixel = min(1280, max(w.intValue, h.intValue))
        for _ in 0..<24 {
            let options: [CFString: Any] = [kCGImageSourceCreateThumbnailFromImageAlways: true,
                kCGImageSourceCreateThumbnailWithTransform: true, kCGImageSourceThumbnailMaxPixelSize: maxPixel,
                kCGImageSourceShouldCacheImmediately: true]
            guard let image = CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary) else {
                throw WorkspaceError.invalid("This image could not be opened.")
            }
            if image.height > 720 { maxPixel = max(1, Int(Double(maxPixel) * 720 / Double(image.height))); continue }
            let encoded = NSMutableData()
            guard let output = CGImageDestinationCreateWithData(encoded, UTType.png.identifier as CFString, 1, nil) else {
                throw WorkspaceError.invalid("This image could not be imported.")
            }
            CGImageDestinationAddImage(output, image, nil)
            guard CGImageDestinationFinalize(output) else { throw WorkspaceError.invalid("This image could not be imported.") }
            if encoded.length <= 1048576 {
                let name = String(url.lastPathComponent.filter { !$0.isNewline && !$0.isASCII || ($0.asciiValue ?? 32) >= 32 }.prefix(80))
                return CardContent(name: name.isEmpty ? "Image" : name,
                    html: "<img src=\"callayers-image:local\" style=\"display:block;width:100%;height:100%;object-fit:contain\">",
                    width: Double(image.width), height: Double(image.height),
                    image: "data:image/png;base64," + (encoded as Data).base64EncodedString())
            }
            guard maxPixel > 1 else { break }; maxPixel = max(1, Int(Double(maxPixel) * 0.75))
        }
        throw WorkspaceError.invalid("Choose a smaller image and try again.")
    }
}
