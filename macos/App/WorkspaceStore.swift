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
        _ = try document.validated()
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        let data = try encoder.encode(document)
        guard data != lastSaved else { return }
        try manager.createDirectory(at: directory, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
        if let previous = lastSaved { try previous.write(to: backup, options: .atomic) }
        try data.write(to: file, options: .atomic)
        lastSaved = data
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
