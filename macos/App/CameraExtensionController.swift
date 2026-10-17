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
    private var checking = false

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

    func prepareRelease() {
        guard Bundle.main.object(forInfoDictionaryKey: "CallayersRelease") as? String == "YES", canInstall else { return }
        checking = true; busy = true
        let request = OSSystemExtensionRequest.propertiesRequest(forExtensionWithIdentifier: Self.identifier, queue: .main)
        pending = request; request.delegate = self
        OSSystemExtensionManager.shared.submitRequest(request)
    }
    nonisolated func request(_ request: OSSystemExtensionRequest, foundProperties properties: [OSSystemExtensionProperties]) {
        let current = properties.filter { $0.isEnabled || $0.isAwaitingUserApproval }.max { $0.bundleVersion.compare($1.bundleVersion, options: .numeric) == .orderedAscending }
        let version = current?.bundleVersion, approval = current?.isAwaitingUserApproval ?? false
        Task { @MainActor in
            guard pending === request else { return }
            checking = false; busy = false; pending = nil
            let extensionURL = Bundle.main.bundleURL.appendingPathComponent("Contents/Library/SystemExtensions/" + Self.identifier + ".systemextension")
            guard let bundled = Bundle(url: extensionURL)?.object(forInfoDictionaryKey: "CFBundleVersion") as? String else { return }
            if approval { needsApproval = true; message = "Allow Callayers Camera in System Settings to finish setup."; return }
            if version == nil || bundled.compare(version!, options: .numeric) == .orderedDescending { install() }
        }
    }
    func install() { submit(removing: false) }
    func remove() { submit(removing: true) }
    private func submit(removing: Bool) {
        guard canInstall else { return }
        let camera = CLVirtualCameraClient(); camera.refresh()
        guard camera.consumerCount == 0 else { message = "Finish using Callayers in your calling app, then try camera setup again."; return }
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
            guard pending === request else { return }
            needsApproval = true
            message = "Allow Callayers in System Settings. On macOS 15, open General → Login Items & Extensions → Camera Extensions."
        }
    }
    nonisolated func request(_ request: OSSystemExtensionRequest, actionForReplacingExtension existing: OSSystemExtensionProperties,
                 withExtension replacement: OSSystemExtensionProperties) -> OSSystemExtensionRequest.ReplacementAction {
        replacement.bundleVersion.compare(existing.bundleVersion, options: .numeric) == .orderedAscending ? .cancel : .replace
    }
    nonisolated func request(_ request: OSSystemExtensionRequest, didFinishWithResult result: OSSystemExtensionRequest.Result) {
        let restart = result == .willCompleteAfterReboot
        Task { @MainActor in
            guard pending === request else { return }
            if checking { checking = false; busy = false; pending = nil; return }
            busy = false
            pending = nil
            needsApproval = false
            if restart {
                message = "Restart your Mac to finish \(removing ? "removing" : "activating") the Callayers camera."
            } else {
                message = removing ? "Callayers camera removed." : "Camera activated. Choose your webcam in the workspace and select Callayers in your calling app."
            }
        }
    }
    nonisolated func request(_ request: OSSystemExtensionRequest, didFailWithError error: Error) {
        let failure = error as NSError
        let detail = "Camera setup could not finish. \(failure.localizedDescription) Try Install camera again."
        Task { @MainActor in
            guard pending === request else { return }
            busy = false
            pending = nil
            needsApproval = false
            checking = false; message = detail
        }
    }
    func openSettings() {
        if let url = URL(string: "x-apple.systempreferences:com.apple.LoginItems-Settings.extension") { NSWorkspace.shared.open(url) }
    }
}
