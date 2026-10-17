import SwiftUI
struct AnnotationToolbar: View {
    @ObservedObject var controller: AnnotationController
    @State private var confirmClear = false
    let icons: [AnnotationTool:String] = [.select:"cursorarrow",.pen:"pencil.tip",.rectangle:"rectangle",.ellipse:"circle",.line:"line.diagonal",.arrow:"arrow.up.right",.text:"textformat",.eraser:"eraser"]
    var body: some View {
        HStack(spacing:8) {
            ForEach(AnnotationTool.allCases,id:\.self) { tool in
                Button { controller.tool = tool } label: {
                    Image(systemName:icons[tool] ?? "pencil").frame(width:25,height:25)
                        .foregroundColor(controller.tool == tool ? .black : .white)
                        .background(controller.tool == tool ? Color.cyan : Color.clear).cornerRadius(5)
                }.buttonStyle(.plain).help(tool.rawValue.capitalized).accessibilityLabel(tool.rawValue.capitalized)
            }
            Divider().frame(height:24)
            ForEach(["#ffffff","#ffd166","#ef476f","#06d6a0","#38bdf8","#a69bff"],id:\.self) { hex in
                Button { controller.color = hex } label: {
                    Circle().fill(Color(nsColor:AnnotationRenderer.color(hex))).frame(width:18,height:18)
                        .overlay(Circle().stroke(controller.color == hex ? Color.white : Color.clear,lineWidth:2).padding(-3))
                }.buttonStyle(.plain).help(hex)
            }
            Picker("Size",selection:$controller.size) { Text("Small").tag(3.0); Text("Medium").tag(6.0); Text("Large").tag(10.0) }.frame(width:125)
            if controller.tool == .text || controller.selected?.tool == .text {
                Picker("Text",selection:$controller.font) { Text("24").tag(24.0); Text("36").tag(36.0); Text("54").tag(54.0) }.frame(width:90)
            }
            Spacer(minLength:0)
            Button("Clear annotations") { confirmClear = true }
        }.padding(10).background(Color(white:0.16)).cornerRadius(8)
        .alert("Clear all annotations?",isPresented:$confirmClear) {
            Button("Cancel",role:.cancel) { }; Button("Clear",role:.destructive) { controller.clear() }
        } message: { Text("Cards stay in place. Cleared annotations cannot be restored.") }
    }
}
