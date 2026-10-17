import Foundation

// Mutations and renderer snapshot ownership survive asynchronous disk writes.
// Received content is never held only in a disposable snapshot callback.
struct WorkspaceSaveFlow {
    private var change: ((inout WorkspaceDocument)->Void)?
    private var freshSnapshot = false
    mutating func begin(_ change: ((inout WorkspaceDocument)->Void)?) { self.change = change }
    mutating func applyPending(to document: inout WorkspaceDocument) { change?(&document); change = nil }
    mutating func requestFreshSnapshot() { freshSnapshot = true }
    mutating func consumeFreshSnapshot() -> Bool { let value = freshSnapshot; freshSnapshot = false; return value }
}
