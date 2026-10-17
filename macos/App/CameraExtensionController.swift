import AppKit
import Combine
import SystemExtensions
import Security

@MainActor
final class CameraExtensionController: NSObject, ObservableObject, OSSystemExtensionRequestDelegate {
    static let identifier = "com.callayers.renderer-preview.camera"
    @Published private(set) var message = "Install the Callayers camera, then select it in your calling app."
    @Published private(set) var busy = false
    @Published private(set) var needsApproval = false
    private var pending: OSSystemExtensionRequest?
    private var removing = false

    var canInstall: Bool { !busy && signedForCamera && installedInApplications }
    var prerequisite: String? {
        if !signedForCamera { return "Camera setup requires a build signed with your Apple Developer team." }
        if !installedInApplications { return "Move this app into Applications, then reopen it to set up the camera." }
        return nil
    }
    private var installedInApplications: Bool {
        Bundle.main.bundleURL.resolvingSymlinksInPath().path.hasPrefix("/Applications/")
    }
    private var signedForCamera: Bool {
        var code: SecCode?
        var staticCode: SecStaticCode?
        var info: CFDictionary?
        guard SecCodeCopySelf([], &code) == errSecSuccess, let code,
              SecCodeCopyStaticCode(code, [], &staticCode) == errSecSuccess, let staticCode,
              SecCodeCopySigningInformation(staticCode, SecCSFlags(rawValue: kSecCSSigningInformation), &info) == errSecSuccess,
              let dictionary = info as? [String: Any],
              let team = dictionary[kSecCodeInfoTeamIdentifier as String] as? String, !team.isEmpty,
              let entitlements = dictionary[kSecCodeInfoEntitlementsDict as String] as? [String: Any]
        else { return false }
        return entitlements["com.apple.developer.system-extension.install"] as? Bool == true
    }

    func install() { submit(removing: false) }
    func remove() { submit(removing: true) }
    private func submit(removing: Bool) {
        guard canInstall else { return }
        self.removing = removing
        busy = true
        needsApproval = false
        message = removing ? "Removing Callayers camera…" : "Requesting camera activation…"
        let request = removing
            ? OSSystemExtensionRequest.deactivationRequest(forExtensionWithIdentifier: Self.identifier, queue: .main)
            : OSSystemExtensionRequest.activationRequest(forExtensionWithIdentifier: Self.identifier, queue: .main)
        pending = request
        request.delegate = self
        OSSystemExtensionManager.shared.submitRequest(request)
    }
    nonisolated func requestNeedsUserApproval(_ request: OSSystemExtensionRequest) {
        Task { @MainActor in
            needsApproval = true
            message = "Allow Callayers in System Settings. On macOS 15, open General → Login Items & Extensions → Camera Extensions."
        }
    }
    nonisolated func request(_ request: OSSystemExtensionRequest, actionForReplacingExtension existing: OSSystemExtensionProperties,
                 withExtension replacement: OSSystemExtensionProperties) -> OSSystemExtensionRequest.ReplacementAction {
        .replace
    }
    nonisolated func request(_ request: OSSystemExtensionRequest, didFinishWithResult result: OSSystemExtensionRequest.Result) {
        let restart = result == .willCompleteAfterReboot
        Task { @MainActor in
            busy = false
            pending = nil
            needsApproval = false
            if restart {
                message = "Restart your Mac to finish \(removing ? "removing" : "activating") the Callayers camera."
            } else {
                message = removing ? "Callayers camera removed." : "Camera activated. Select Callayers in your calling app to see the moving test picture."
            }
        }
    }
    nonisolated func request(_ request: OSSystemExtensionRequest, didFailWithError error: Error) {
        let failure = error as NSError
        let detail = "Camera setup failed: \(failure.localizedDescription) (\(failure.domain), \(failure.code)). You can retry installation."
        Task { @MainActor in
            busy = false
            pending = nil
            needsApproval = false
            message = detail
        }
    }
    func openSettings() {
        if let url = URL(string: "x-apple.systempreferences:com.apple.LoginItems-Settings.extension") { NSWorkspace.shared.open(url) }
    }
}
