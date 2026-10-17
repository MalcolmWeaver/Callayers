import AppKit
import Combine
import Sparkle

@MainActor
final class UpdateController: NSObject, ObservableObject, SPUUpdaterDelegate {
    @Published private(set) var message = "Updates are available in installed release builds."
    @Published private(set) var ready = false
    weak var session: ApplicationSession?
    private var controller: SPUStandardUpdaterController?
    private var install: (() -> Void)?
    private var started = false
    private var availabilityTimer: Timer?
    @Published private(set) var canCheck = false
    @Published private(set) var canInstall = false
    deinit { availabilityTimer?.invalidate() }
    private func refreshAvailability() {
        let check = controller?.updater.canCheckForUpdates ?? false
        if canCheck != check { canCheck = check }
        let install = installationAllowed
        if canInstall != install { canInstall = install }
    }
    func start() {
        guard !started else { return }; started = true
        guard Bundle.main.object(forInfoDictionaryKey: "CallayersRelease") as? String == "YES",
              let publicKey = Bundle.main.object(forInfoDictionaryKey: "SUPublicEDKey") as? String,
              Data(base64Encoded: publicKey)?.count == 32,
              Bundle.main.bundleURL.resolvingSymlinksInPath().path.hasPrefix("/Applications/") else { return }
        let updater = SPUStandardUpdaterController(startingUpdater: false, updaterDelegate: self, userDriverDelegate: nil)
        controller = updater
        availabilityTimer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.refreshAvailability() }
        }
        do { try updater.updater.start(); message = "Updates are checked automatically."; refreshAvailability() }
        catch { message = "Couldn't start update checks. Try again after reopening Callayers." }
    }
    func check() {
        guard let controller else { return }
        controller.checkForUpdates(nil)
    }
    private var installationAllowed: Bool {
        guard ready, session?.camera?.hasConsumers != true else { return false }
        guard let model = session?.workspace else { return true }
        return !model.video.hasConsumers && model.editable && !model.generating
    }
    func installWhenIdle() {
        guard installationAllowed else { return }
        install?()
    }
    func feedURLString(for updater: SPUUpdater) -> String? {
        #if arch(arm64)
        return "https://downloads.callayers.com/macos/arm64/appcast.xml"
        #else
        return "https://downloads.callayers.com/macos/x86_64/appcast.xml"
        #endif
    }
    func updater(_ updater: SPUUpdater, mayPerform updateCheck: SPUUpdateCheck) throws {
        if session?.camera?.hasConsumers == true {
            throw NSError(domain: "CallayersUpdate", code: 1, userInfo: [NSLocalizedDescriptionKey: "Finish your call before checking for updates."])
        }
    }
    func updater(_ updater: SPUUpdater, willInstallUpdateOnQuit item: SUAppcastItem,
                 immediateInstallationBlock immediateInstallHandler: @escaping () -> Void) -> Bool {
        install = immediateInstallHandler; ready = true; refreshAvailability()
        message = "Update ready. It will install when you quit Callayers."
        return true
    }
    func updater(_ updater: SPUUpdater, shouldPostponeRelaunchForUpdate item: SUAppcastItem,
                 untilInvokingBlock installHandler: @escaping () -> Void) -> Bool {
        if session?.camera?.hasConsumers == true || session?.workspace?.generating == true || session?.workspace?.importing == true {
            install = installHandler; ready = true; refreshAvailability(); message = "Update ready. Finish your call and edits before restarting."
            return true
        }
        return false
    }
    func allowedSystemProfileKeys(for updater: SPUUpdater) -> [String]? { [] }
    func updater(_ updater: SPUUpdater, didAbortWithError error: Error) {
        message = "Couldn't check for updates. You can try again later."
    }
}
