import SwiftUI

enum AppTheme {
    static let background = Color(red:0.11,green:0.11,blue:0.13)
    static let sidebar = Color(red:0.14,green:0.14,blue:0.16)
    static let accent = Color(red:0.65,green:0.60,blue:0.98)
}
struct CallayersMark: View {
    var size: CGFloat = 34
    var body: some View {
        Group {
            if let path = Bundle.main.path(forResource:"callayers",ofType:"png"), let image = NSImage(contentsOfFile:path) {
                Image(nsImage:image).resizable().scaledToFit()
            } else { Image(systemName:"square.stack.3d.up.fill").resizable().scaledToFit().foregroundColor(AppTheme.accent) }
        }.frame(width:size,height:size)
    }
}
