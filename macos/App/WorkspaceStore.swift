import Foundation

// Foundation-only storage, also exercised by WorkspaceChecks without the UI.
final class WorkspaceStore {
    let directory: URL
    var file: URL { directory.appendingPathComponent("workspace.json") }
    var backup: URL { directory.appendingPathComponent("workspace.previous.json") }
    private(set) var writable = false
    private var lastSaved: Data?
    private let manager = FileManager.default

    init(directory: URL) { self.directory = directory }

    static func applicationStore() throws -> WorkspaceStore {
        let support = try FileManager.default.url(for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true)
        return WorkspaceStore(directory: support.appendingPathComponent("Callayers/DevelopmentWorkspace", isDirectory: true))
    }

    private func read(_ url: URL) throws -> (WorkspaceDocument, Data) {
        let attributes = try manager.attributesOfItem(atPath: url.path)
        guard let size = attributes[.size] as? NSNumber, size.intValue <= 16 * 1024 * 1024 else {
            throw WorkspaceError.invalid("The saved workspace is too large to open.")
        }
        let data = try Data(contentsOf: url)
        let document = try JSONDecoder().decode(WorkspaceDocument.self, from: data).validated()
        return (document, data)
    }

    func load() throws -> WorkspaceDocument {
        writable = false
        lastSaved = nil
        do {
            let (document, data) = try read(file)
            lastSaved = data; writable = true
            return document
        } catch {
            let ns = error as NSError
            // Only a genuinely absent file means a new workspace. Other I/O
            // errors must not silently turn existing data into an empty library.
            if ns.domain == NSCocoaErrorDomain && [NSFileNoSuchFileError, NSFileReadNoSuchFileError].contains(ns.code) {
                guard !manager.fileExists(atPath: backup.path) else {
                    throw WorkspaceError.invalid("The workspace file is missing, but a saved backup is available.")
                }
                writable = true
                return WorkspaceDocument()
            }
            throw error
        }
    }

    var canRecoverBackup: Bool { (try? read(backup)) != nil }

    func save(_ document: WorkspaceDocument) throws {
        guard writable else { throw WorkspaceError.locked }
        let document = try document.validated()
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        let data = try encoder.encode(document)
        guard data.count <= 16 * 1024 * 1024 else { throw WorkspaceError.invalid("The workspace is too large to save.") }
        guard data != lastSaved else { return }
        try manager.createDirectory(at: directory, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
        if let previous = lastSaved { try previous.write(to: backup, options: .atomic) }
        try data.write(to: file, options: .atomic)
        lastSaved = data
        pruneUnusedImages(document)
    }

    private func pruneUnusedImages(_ document: WorkspaceDocument) {
        // A preserved unreadable workspace may reference assets we cannot enumerate.
        guard let names = try? manager.contentsOfDirectory(atPath: directory.path),
              !names.contains(where: { $0.hasPrefix("workspace-preserved-") }) else { return }
        var documents = [document]
        if manager.fileExists(atPath: backup.path) {
            guard let previous = try? read(backup).0 else { return }
            documents.append(previous)
        }
        let keep = Set(documents.flatMap { $0.cards.flatMap { [$0.imageID, $0.previousVersion?.imageID].compactMap { $0 } } })
        let images = directory.appendingPathComponent("images", isDirectory: true)
        guard (try? images.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink) == false,
              let assets = try? manager.contentsOfDirectory(at: images, includingPropertiesForKeys: [.isRegularFileKey, .isSymbolicLinkKey]) else { return }
        for asset in assets where asset.pathExtension == "png" {
            guard let id = UUID(uuidString: asset.deletingPathExtension().lastPathComponent), !keep.contains(id),
                  let values = try? asset.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey]),
                  values.isRegularFile == true, values.isSymbolicLink == false else { continue }
            try? manager.removeItem(at: asset)
        }
    }

    private func imageURL(_ id: UUID) -> URL {
        directory.appendingPathComponent("images", isDirectory: true).appendingPathComponent(id.uuidString + ".png")
    }

    func saveImage(_ data: Data, id: UUID) throws {
        guard writable else { throw WorkspaceError.locked }
        guard data.count <= 4 * 1024 * 1024,
              data.starts(with: [137, 80, 78, 71, 13, 10, 26, 10]) else {
            throw WorkspaceError.invalid("The local image is invalid or too large.")
        }
        let url = imageURL(id)
        try manager.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true,
                                    attributes: [.posixPermissions: 0o700])
        // Asset identities are immutable. A failed workspace save can leave an
        // unreferenced asset, but cannot damage a previous card or its backup.
        guard !manager.fileExists(atPath: url.path) else { throw WorkspaceError.invalid("The local image already exists.") }
        try data.write(to: url, options: .atomic)
    }

    func renderedHTML(for card: WorkspaceCard) throws -> String {
        guard let id = card.imageID else { return card.html }
        let url = imageURL(id)
        let attributes = try manager.attributesOfItem(atPath: url.path)
        guard let size = attributes[.size] as? NSNumber, size.intValue <= 4 * 1024 * 1024 else {
            throw WorkspaceError.invalid("The saved image is too large.")
        }
        let data = try Data(contentsOf: url)
        guard data.starts(with: [137, 80, 78, 71, 13, 10, 26, 10]) else { throw WorkspaceError.invalid("The saved image is unreadable.") }
        let occurrences = card.html.components(separatedBy: "callayers-image:local").count - 1
        guard card.html.utf8.count + occurrences * (4 * ((data.count + 2) / 3) + 22) <= 6 * 1024 * 1024 else {
            throw WorkspaceError.invalid("The card contains too many image references.")
        }
        return card.html.replacingOccurrences(of: "callayers-image:local", with: "data:image/png;base64," + data.base64EncodedString())
    }

    func recoverBackup() throws -> WorkspaceDocument {
        let (document, data) = try read(backup)
        // Preserve the unreadable original, even if it comes from a newer app.
        if manager.fileExists(atPath: file.path) {
            let preserved = directory.appendingPathComponent("workspace-preserved-\(UUID().uuidString).json")
            try manager.copyItem(at: file, to: preserved)
        }
        try data.write(to: file, options: .atomic)
        lastSaved = data; writable = true
        return document
    }
}
