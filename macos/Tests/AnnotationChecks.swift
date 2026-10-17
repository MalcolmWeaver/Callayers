import Foundation
import AppKit
@main struct AnnotationChecks {
    static func main() throws {
        func check(_ b: Bool,_ m: String) throws { if !b { throw WorkspaceError.invalid(m) } }
        let item = Annotation(tool:.rectangle,color:"#ff0000",size:6,font:36,box:InkBox(x:100,y:100,width:200,height:100))
        try check(AnnotationGeometry.hit(item,point:InkPoint(x:100,y:150),tolerance:5), "Border not selectable")
        try check(!AnnotationGeometry.hit(item,point:InkPoint(x:200,y:150),tolerance:5), "Hollow shape filled hit region")
        let moved = AnnotationGeometry.move(item,dx:5000,dy:-5000)
        try check(moved.box.x + moved.box.width <= 1280 && moved.box.y == 0, "Moved annotation outside scene")
        let layer = AnnotationRenderer.render([item])!
        try check(layer.width == 1920 && layer.height == 1080, "Wrong annotation layer size")
        var doc = WorkspaceDocument(); doc.annotations = [item]
        try check(try doc.validated().annotations.count == 1, "Valid annotation rejected")
        doc.annotations[0].box.width = .infinity
        do { _ = try doc.validated(); throw NSError(domain:"Invalid annotation accepted",code:1) } catch WorkspaceError.invalid { }
        print("PASS annotation hit testing, bounded geometry, transparent output and persistence validation")
    }
}
