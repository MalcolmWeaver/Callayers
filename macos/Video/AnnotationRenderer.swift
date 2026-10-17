import AppKit

// The same typography is used by the private editor and completed video layer.
enum AnnotationRenderer {
    static func color(_ value: String) -> NSColor {
        let hex = UInt32(value.dropFirst(),radix:16) ?? 0xffd166
        return NSColor(srgbRed:CGFloat((hex>>16)&255)/255,green:CGFloat((hex>>8)&255)/255,blue:CGFloat(hex&255)/255,alpha:1)
    }
    static func textAttributes(_ item: Annotation, scale: CGFloat = 1) -> [NSAttributedString.Key:Any] {
        let paragraph = NSMutableParagraphStyle(); paragraph.lineBreakMode = .byWordWrapping
        return [.font:NSFont.systemFont(ofSize:item.font*scale),.foregroundColor:color(item.color),.paragraphStyle:paragraph]
    }
    static func draw(_ items: [Annotation], in context: CGContext) {
        for item in items {
            if item.tool == .text {
                let b = item.box
                (item.text as NSString).draw(in:NSRect(x:b.x,y:b.y,width:b.width,height:b.height),withAttributes:textAttributes(item))
            } else {
                context.addPath(AnnotationGeometry.path(item)); context.setStrokeColor(color(item.color).cgColor)
                context.setLineWidth(item.size); context.setLineCap(.round); context.setLineJoin(.round); context.strokePath()
            }
        }
    }
    static func render(_ items: [Annotation]) -> CGImage? {
        guard !items.isEmpty, let context = CGContext(data:nil,width:1920,height:1080,bitsPerComponent:8,bytesPerRow:0,
            space:CGColorSpace(name:CGColorSpace.sRGB)!,bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        context.scaleBy(x:1.5,y:1.5); context.translateBy(x:0,y:720); context.scaleBy(x:1,y:-1)
        NSGraphicsContext.saveGraphicsState(); NSGraphicsContext.current = NSGraphicsContext(cgContext:context,flipped:true)
        draw(items,in:context); NSGraphicsContext.restoreGraphicsState(); return context.makeImage()
    }
}
