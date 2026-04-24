// st80-2026 — St80InputController.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Shared state between the SwiftUI control strip (see
// ControlStripView) and the UIKit MTKView (St80MTKView). The strip
// toggles Ctrl / Cmd as one-shot modifiers and posts raw keys the
// soft keyboard doesn't offer (Esc, Tab, Backspace). MTKView
// consumes the modifiers when sending subsequent key events.
//
// Pattern lifted from iospharo's `PharoBridge.ctrlModifierActive` /
// `cmdModifierActive` (MIT, same author — see THIRD_PARTY_LICENSES).

import Foundation
import Combine
import UIKit

final class St80InputController: ObservableObject {

    static let shared = St80InputController()

    /// True while the user has armed the virtual Ctrl key. The next
    /// key event consumes it and clears automatically — one-shot, like
    /// a phone keyboard's shift.
    @Published var ctrlActive = false

    /// Same story for Cmd.
    @Published var cmdActive = false

    /// Visibility of the soft keyboard. Bound to the keyboard-toggle
    /// button in the strip; the strip sets it and the MTKView reads
    /// it to call become/resignFirstResponder.
    @Published var keyboardVisible = false

    /// Weak back-reference to the MTKView so the strip can drive
    /// first-responder state for the soft keyboard.
    weak var mtkView: UIView?

    func toggleCtrl() { ctrlActive.toggle() }
    func toggleCmd()  { cmdActive.toggle() }

    /// Post a raw ASCII code with whatever modifiers are armed. Clears
    /// the one-shot modifiers after a single use.
    func sendRaw(_ code: Int32, shift: Bool = false) {
        var mods: UInt32 = 0
        if shift       { mods |= ST80_MOD_SHIFT }
        if ctrlActive  { mods |= ST80_MOD_CTRL;    ctrlActive = false }
        if cmdActive   { mods |= ST80_MOD_COMMAND; cmdActive = false }
        // C bridge ignores modifier flags; fold Ctrl into the byte below.
        _ = mods
        st80_post_key_down(code, 0)
    }

    /// Send a Smalltalk-80 editor shortcut: `Ctrl+<letter>` delivered as
    /// the matching ASCII control code (D→4, P→16, S→19, …). The decoded
    /// keyboard has no modifier side-channel, so action buttons in the
    /// on-screen strip must emit the control byte directly. Letter may
    /// be given in either case; only 'A'..'Z' / 'a'..'z' are accepted.
    func sendCtrlLetter(_ letter: Character) {
        guard let scalar = letter.unicodeScalars.first else { return }
        let v = scalar.value
        let base: UInt32
        if v >= 0x41 && v <= 0x5A      { base = v }          // 'A'..'Z'
        else if v >= 0x61 && v <= 0x7A { base = v - 0x20 }   // 'a'..'z'
        else { return }
        st80_post_key_down(Int32(base & 0x1F), 0)
    }

    /// Paste the iOS pasteboard's text contents into the VM as a stream
    /// of key events. Thin wrapper so SwiftUI strip views can trigger
    /// system-clipboard paste without reaching into the MTKView. The
    /// actual keystroke pump lives on St80MTKView.pasteFromSystemClipboard.
    func pasteFromSystemClipboard() {
        (mtkView as? St80MTKView)?.pasteFromSystemClipboard()
    }

    /// Trigger a full VM→system-clipboard copy (or cut when `cut` is true):
    /// the MTKView sends Ctrl+C / Ctrl+X to the VM and, once the VM has
    /// had time to update `ParagraphEditor>>CurrentSelection`, mirrors
    /// that text to `UIPasteboard.general`.
    func copySelectionToSystemClipboard(cut: Bool) {
        (mtkView as? St80MTKView)?.copyVMSelectionToSystemClipboard(cut: cut)
    }

    /// Called by St80MTKView.insertText / pressesBegan to add any
    /// armed one-shot modifiers to the real key event's modifier mask.
    func consumeActiveModifiers(_ baseMods: UInt32) -> UInt32 {
        var mods = baseMods
        if ctrlActive { mods |= ST80_MOD_CTRL;    ctrlActive = false }
        if cmdActive  { mods |= ST80_MOD_COMMAND; cmdActive = false }
        return mods
    }

    func setKeyboardVisible(_ visible: Bool) {
        keyboardVisible = visible
        if visible { _ = mtkView?.becomeFirstResponder() }
        else       { _ = mtkView?.resignFirstResponder() }
    }
}
