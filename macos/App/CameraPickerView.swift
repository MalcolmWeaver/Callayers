import SwiftUI

struct CameraPickerView: View {
    @ObservedObject var video: VideoModel
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Picker("Webcam", selection: Binding(get: { video.selectedID }, set: { video.choose($0) })) {
                    Text("Choose webcam…").tag("")
                    if !video.selectedID.isEmpty && !video.cameras.contains(where: { $0.id == video.selectedID }) {
                        Text("Saved webcam (unavailable)").tag(video.selectedID)
                    }
                    ForEach(video.cameras) { camera in Text(camera.name).tag(camera.id) }
                }.frame(maxWidth: 400)
                Button("Retry camera") { video.retry() }
            }
            Text(video.cameraStatus).font(.caption).foregroundColor(.secondary)
            Text(video.outputStatus).font(.caption).foregroundColor(.secondary)
        }
    }
}
