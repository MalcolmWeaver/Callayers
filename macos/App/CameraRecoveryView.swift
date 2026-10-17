import SwiftUI
struct CameraRecoveryView: View {
    @ObservedObject var video: VideoModel
    let signOut: ()->Void
    var body: some View {
        VStack(spacing:20) {
            CallayersMark(size:58)
            Text("Choose your webcam").font(.largeTitle.weight(.semibold))
            Text("Connect a working camera to open your workspace. Your cards stay saved.").foregroundColor(.secondary)
            CameraPickerView(video:video).frame(maxWidth:520)
            Button("Sign out",action:signOut).padding(.top,10)
        }.padding(40).frame(maxWidth:.infinity,maxHeight:.infinity).background(AppTheme.background)
    }
}
