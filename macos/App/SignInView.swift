import SwiftUI
struct SignInView: View {
    @ObservedObject var auth: AuthController
    var body: some View {
        VStack(spacing:22) {
            CallayersMark(size:72)
            Text("Callayers").font(.system(size:36,weight:.semibold))
            Text("Put your ideas in the picture.").font(.title3).foregroundColor(.secondary)
            Button { auth.signIn() } label: { Text("Sign in with Google").fontWeight(.semibold).padding(.horizontal,28).padding(.vertical,10) }
                .buttonStyle(.borderedProminent).tint(AppTheme.accent).disabled(auth.busy)
            if auth.busy { ProgressView(); Button("Cancel") { auth.cancel() } }
            Text(auth.message).foregroundColor(.secondary).multilineTextAlignment(.center).frame(maxWidth:440)
        }.frame(maxWidth:.infinity,maxHeight:.infinity).background(AppTheme.background)
    }
}
