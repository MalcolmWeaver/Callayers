import AppKit

final class AnnotationTextEditor: NSTextView {
    var finished: ((String?)->Void)?
    private var ending = false
    func finish(_ value: String?) { guard !ending else { return }; ending = true; finished?(value) }
    override func keyDown(with event: NSEvent) {
        if event.keyCode == 53 { finish(nil); return }
        if [36,76].contains(event.keyCode) && !event.modifierFlags.intersection([.command,.control]).isEmpty { finish(string); return }
        super.keyDown(with:event)
    }
    override func resignFirstResponder() -> Bool {
        let result = super.resignFirstResponder()
        if result { finish(string) }; return result
    }
}
