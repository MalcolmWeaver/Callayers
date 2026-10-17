import Foundation
import ImageIO
import UniformTypeIdentifiers

struct ImportedImage {
    let png: Data
    let width: Int
    let height: Int
    let name: String
    static let html = "<img src=\"callayers-image:local\" style=\"display:block;width:100%;height:100%;object-fit:contain\" alt=\"\">"
}

enum ImageImport {
    static let maximumSourceBytes = 32 * 1024 * 1024
    static let maximumStoredBytes = 4 * 1024 * 1024

    static func read(_ url: URL) throws -> ImportedImage {
        let values = try url.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey])
        guard values.isRegularFile == true, let size = values.fileSize, size <= maximumSourceBytes else {
            throw WorkspaceError.invalid("Choose an image smaller than 32 MB.")
        }
        let data = try Data(contentsOf: url, options: .mappedIfSafe)
        return try decode(data, name: url.lastPathComponent)
    }

    static func decode(_ data: Data, name: String) throws -> ImportedImage {
        guard data.count <= maximumSourceBytes,
              let source = CGImageSourceCreateWithData(data as CFData, [kCGImageSourceShouldCache: false] as CFDictionary),
              let type = CGImageSourceGetType(source) as String?,
              [UTType.png.identifier, UTType.jpeg.identifier, UTType.bmp.identifier].contains(type),
              let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              let width = properties[kCGImagePropertyPixelWidth] as? Int,
              let height = properties[kCGImagePropertyPixelHeight] as? Int,
              width > 0, height > 0, width <= 32768, height <= 32768, width * height <= 100_000_000 else {
            throw WorkspaceError.invalid("Couldn't read that image. Choose a PNG, JPEG, or BMP.")
        }
        // Decode at bounded size and apply EXIF orientation before stripping metadata.
        let orientation = properties[kCGImagePropertyOrientation] as? Int ?? 1
        let rotated = (5...8).contains(orientation)
        let w = rotated ? height : width, h = rotated ? width : height
        let scale = min(1, min(1280 / Double(w), 720 / Double(h)))
        let maximum = max(1, Int((Double(max(w, h)) * scale).rounded(.down)))
        let options: [CFString: Any] = [kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceCreateThumbnailWithTransform: true, kCGImageSourceThumbnailMaxPixelSize: maximum,
            kCGImageSourceShouldCacheImmediately: true]
        guard let image = CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary),
              image.width <= 1280, image.height <= 720 else {
            throw WorkspaceError.invalid("Couldn't decode that image. Try another file.")
        }
        let output = NSMutableData()
        guard let destination = CGImageDestinationCreateWithData(output, UTType.png.identifier as CFString, 1, nil) else {
            throw WorkspaceError.invalid("Couldn't prepare this image.")
        }
        CGImageDestinationAddImage(destination, image, nil)
        guard CGImageDestinationFinalize(destination), output.length <= maximumStoredBytes else {
            throw WorkspaceError.invalid("This image is too large to save. Try a smaller image.")
        }
        var safeName = name.trimmingCharacters(in: .whitespacesAndNewlines)
        while safeName.utf16.count > 120 { safeName.removeLast() }
        if safeName.isEmpty { safeName = "Image" }
        return ImportedImage(png: output as Data, width: image.width, height: image.height, name: safeName)
    }
}
