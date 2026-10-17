import Foundation

// A queued save callback executes in a timer/disk completion task, not in the
// original generation task. Carry cancellation explicitly to that callback.
@MainActor final class GenerationPermit {
    private(set) var valid = true
    func invalidate() { valid = false }
}
