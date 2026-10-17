import AppKit

// Shared font/layout values for committed rendering and the private text editor.
enum AnnotationRenderer {
    static func color(_ rgb: UInt32) -> NSColor {
        NSColor(srgbRed: CGFloat((rgb >> 16) & 255) / 255, green: CGFloat((rgb >> 8) & 255) / 255,
                blue: CGFloat(rgb & 255) / 255, alpha: 1)
    }
    static func image(_ annotations: [WorkspaceAnnotation]) -> CGImage? {
        guard !annotations.isEmpty,
              let context = CGContext(data: nil, width: 1920, height: 1080, bitsPerComponent: 8, bytesPerRow: 1920 * 4,
                space: CGColorSpace(name: CGColorSpace.sRGB)!,
                bitmapInfo: CGBitmapInfo.byteOrder32Little.rawValue | CGImageAlphaInfo.premultipliedFirst.rawValue) else { return nil }
        context.translateBy(x: 0, y: 1080); context.scaleBy(x: 1.5, y: -1.5)
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: context, flipped: true)
        for item in annotations {
            if item.kind == .text {
                let storage = NSTextStorage(string: item.text, attributes: [.font: NSFont.systemFont(ofSize: item.fontSize),
                    .foregroundColor: color(item.color)])
                let layout = NSLayoutManager(), container = NSTextContainer(size: item.bounds.size)
                container.lineFragmentPadding = 0
                layout.addTextContainer(container); storage.addLayoutManager(layout)
                let range = layout.glyphRange(for: container)
                context.saveGState(); context.clip(to: item.bounds)
                layout.drawGlyphs(forGlyphRange: range, at: item.bounds.origin)
                context.restoreGState()
            } else {
                context.setStrokeColor(color(item.color).cgColor)
                context.setLineWidth(item.thickness); context.setLineCap(.round); context.setLineJoin(.round)
                context.addPath(item.path); context.strokePath()
            }
        }
        NSGraphicsContext.restoreGraphicsState()
        return context.makeImage()
    }
}
