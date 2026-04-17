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
        st80_post_key_down(code, mods)
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
